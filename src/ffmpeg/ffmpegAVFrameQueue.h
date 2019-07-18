#pragma once

#include "ffmpegAVFrameBufferInterfaces.h"
#include "syncpolicies.h"

#include <numeric> // for std::reduce
#include <vector>

namespace ffmpeg
{

// AVFrame queue
template <typename MutexType, typename CondVarType, typename MutexLockType>
class AVFrameQueue : public IAVFrameBuffer
{
  public:
  AVFrameQueue(size_t N = 0) : dynamic(N <= 1), src(nullptr), dst(nullptr)
  {
    // set read/write pointers to the beginning
    wr = rd = que.begin();

    // insert the first frame
    expand();
    if (!dynamic) // if fixed queue size, expand to the desired size
      for (int i = 1; i < N; ++i) expand();

  } // queue size

  AVFrameQueue(const AVFrameQueue &that)
      : src(that.src), dst(that.dst), dynamic(that.dynamic),
        que(that.que.size())
  {
    std::transform(that.que.begin(), that.que.end(), que.begin(),
                   [](const QueData &src) -> QueData {
                     return {av_frame_clone(src.frame), src.eof, src.populated};
                   });
    wr = que.begin() + (that.wr - that.que.begin());
    rd = que.begin() + (that.rd - that.que.begin());
  }

  virtual ~AVFrameQueue()
  {
    // release allocated memory for all the AVFrames
    for (auto &buf : que)
      if (buf.frame) av_frame_free(&buf.frame);
  }

  AVFrameQueue &operator=(const AVFrameQueue &that)
  {
    src = that.src;
    dst = that.dst;
    dynamic = that.dynamic;
    que(that.que.size());
    std::transform(that.que.begin(), that.que.end(), que.begin(),
                   [](const QueData &src) -> QueData {
                     return {av_frame_clone(src.frame), src.eof, src.populated};
                   });
    wr = que.begin() + (that.wr - that.que.begin());
    rd = que.begin() + (that.rd - that.que.begin());
  }

  const MediaParams &getMediaParams() const
  {
    if (src)
      return src->getMediaParams();
    else
      throw Exception(
          "Media parameters could be retrieved only if src is connected.");
  }

  IAVFrameSource &getSrc() const
  {
    if (src)
      return *src;
    else
      throw Exception("No source is connected.");
  };
  void setSrc(IAVFrameSource &buf) { src = &buf; }
  void clrSrc() { src = nullptr; }

  IAVFrameSink &getDst() const
  {
    if (dst)
      return *dst;
    else
      throw Exception("No source is connected.");
  };
  void setDst(IAVFrameSink &buf) { dst = &buf; }
  void clrDst() { dst = nullptr; }

  bool autoexpand() const { return dynamic; }

  bool ready() const { return true; };

  void clear()
  {
    MutexLockType lock(mutex);
    for (auto it = que.begin(); it != que.end(); ++it)
    {
      if (it->populated)
      {
        av_frame_unref(it->frame);
        it->populated = false;
        it->eof = false;
      }
    }
    wr = rd = que.begin();
  }

  size_t size() noexcept
  {
    MutexLockType lock(mutex);
    return std::reduce(que.begin(), que.end(), 0ull,
                       [](const size_t count, const auto &elem) {
                         return count + elem.populated;
                       });
  }
  bool empty() noexcept
  {
    MutexLockType lock(mutex);
    return !((que.size() > 1 && wr != que.begin()) ? (wr - 1)->populated
                                                   : que.back().populated);
  }
  bool full() noexcept
  {
    if (dynamic) return false;
    MutexLockType lock(mutex);
    return wr->populated;
  }

  ///////////////////////////////////////////////////////////////////////////////

  bool readyToPush()
  {
    MutexLockType lock(mutex);
    return readyToPush_threadunsafe();
  }

  void blockTillReadyToPush()
  {
    MutexLockType lock(mutex);
    cv_rx.wait(lock, [this] { return readyToPush_threadunsafe(); });
  }

  bool blockTillReadyToPush(const std::chrono::milliseconds &rel_time)
  {
    MutexLockType lock(mutex);
    return cv_rx.wait_for(lock, rel_time,
                          [this] { return readyToPush_threadunsafe(); });
  }

  AVFrame *peekToPush()
  {
    MutexLockType lock(mutex);
    cv_rx.wait(lock, [this] { return readyToPush_threadunsafe(); });
    if (wr->populated)
      throw_or_expand(); // expand if allowed or throws overflow exception
    return wr->frame;
  }

  void push()
  {
    MutexLockType lock(mutex);
    cv_rx.wait(lock, [this] { return readyToPush_threadunsafe(); });
    mark_populated_threadunsafe();
  }

  void push(AVFrame *frame)
  {
    MutexLockType lock(mutex);
    cv_rx.wait(lock, [this] { return readyToPush_threadunsafe(); });
    push_threadunsafe(frame);
  }

  bool push(AVFrame *frame, const std::chrono::milliseconds &rel_time)
  {
    MutexLockType lock(mutex);
    bool success = cv_rx.wait_for(
        lock, rel_time, [this] { return readyToPush_threadunsafe(); });
    if (success) push_threadunsafe(frame);
    return success;
  }

  bool tryToPush(AVFrame *frame)
  {
    MutexLockType lock(mutex);
    if (readyToPush_threadunsafe())
    {
      push_threadunsafe(frame);
      return true;
    }
    else
    {
      return false;
    }
  }

  ///////////////////////////////////////////////////////////////////////////////

  bool readyToPop()
  {
    MutexLockType lock(mutex);
    return readyToPop_threadunsafe();
  }

  void blockTillReadyToPop()
  {
    MutexLockType lock(mutex);
    cv_tx.wait(lock, [this] { return readyToPop_threadunsafe(); });
  }

  bool blockTillReadyToPop(const std::chrono::milliseconds &rel_time)
  {
    MutexLockType lock(mutex);
    return cv_tx.wait_for(lock, rel_time,
                          [this] { return readyToPop_threadunsafe(); });
  }

  void pop(AVFrame *frame, bool *eof = nullptr)
  {
    if (!frame) throw Exception("frame must be non-null pointer.");
    MutexLockType lock(mutex);
    cv_tx.wait(lock, [this] { return readyToPop_threadunsafe(); });
    pop_threadunsafe(frame, eof);
  }

  bool pop(AVFrame *frame, bool *eof, const std::chrono::milliseconds &rel_time)
  {
    if (!frame) throw Exception("frame must be non-null pointer.");
    MutexLockType lock(mutex);
    cv_tx.wait(lock, [this] { return readyToPop_threadunsafe(); });
    bool success = cv_tx.wait_for(lock, rel_time,
                                  [this] { return readyToPop_threadunsafe(); });
    if (success) pop_threadunsafe(frame, eof);
    return success;
  }

  bool eof()
  {
    if (empty()) return false;
    MutexLockType lock(mutex);
    cv_tx.wait(lock, [this] { return readyToPop_threadunsafe(); });
    return rd->eof;
  }

  bool eof(const std::chrono::milliseconds &rel_time)
  {
    if (empty()) return false;
    MutexLockType lock(mutex);
    if (!cv_tx.wait_for(lock, rel_time,
                        [this] { return readyToPop_threadunsafe(); }))
      throw Exception("Timed out while waiting to check for eof.");
    return rd->eof; // true if no more frames in the buffer
  }

  bool tryToPop(AVFrame *frame, bool *eof)
  {
    MutexLockType lock(mutex);
    if (readyToPop_threadunsafe())
    {
      pop_threadunsafe(frame, eof);
      return true;
    }
    else
    {
      return false;
    }
  }

  AVFrame *peekToPop()
  {
    MutexLockType lock(mutex);
    cv_tx.wait(lock, [this] { return readyToPop_threadunsafe(); });

    if (rd->eof)
      return nullptr;
    else
      return rd->frame;
  }
  void pop()
  {
    MutexLockType lock(mutex);
    cv_tx.wait(lock);
    pop_threadunsafe(nullptr, nullptr);
  }

  private:
  void throw_or_expand()
  {
    if (dynamic)
      expand();
    else
      throw Exception("AVFrameQueue::Buffer overflow.");
  }
  void expand()
  {
    int64_t Iwr = wr - que.begin();
    int64_t Ird = rd - que.begin();
    if (rd > wr) ++Ird;

    if (que.empty() || wr == que.end() - 1)
      que.push_back({av_frame_alloc(), false, false});
    else
      que.insert(wr + 1, {av_frame_alloc(), false, false});

    wr = que.begin() + Iwr;
    rd = que.begin() + Ird;

    // advance write pointer to the new element for the subsequent pushing
    if (wr->populated && ++wr == que.end()) wr = que.begin();
  }

  bool readyToPush_threadunsafe() { return dynamic || !wr->populated; }
  bool readyToPop_threadunsafe() // declared in AVFrameSourceBase
  {
    return rd->populated;
  }

  /**
   * \brief Implements enquing of the new frame onto the queue
   * \param[in] frame pointer to the frame data. If null, eof is pushed.
   */
  void push_threadunsafe(AVFrame *frame)
  {
    // if buffer is not available (not yet read, caught up with rd ptr)
    if (wr->populated)
      throw_or_expand(); // expand if allowed or throws overflow exception

    // copy the frame data
    if (frame) av_frame_ref(wr->frame, frame);
    wr->eof = !frame;

    // set the written flag
    wr->populated = true;

    // increment write iterator
    if (++wr == que.end()) wr = que.begin();

    // notify the source-end for the arrival of new data
    cv_tx.notify_one();
  }

  void mark_populated_threadunsafe()
  {
    // if buffer is not available
    if (wr->populated) throw Exception("Already populated.");

    // set the written flag
    wr->populated = true;

    // increment write iterator
    if (++wr == que.end()) wr = que.begin();

    // notify the source-end for the arrival of new data
    cv_tx.notify_one();
  }

  void pop_threadunsafe(AVFrame *frame, bool *eofout)
  {
    // guaranteed readyToPop() returns true

    // grab the eof flag
    bool eof = rd->eof;

    // get the frame if not eof
    if (!eof)
    {
      if (frame)
        av_frame_move_ref(frame, rd->frame);
      else
        av_frame_unref(rd->frame);
      rd->populated = false;
    }

    // notify the sink-end for slot opening
    cv_rx.notify_one();

    // increment the read pointer only if wr pointer is elsewhere
    if (++rd == que.end()) rd = que.begin();

    if (eofout) *eofout = eof;
  }

  IAVFrameSource *src;
  IAVFrameSink *dst;

  MutexType mutex;
  CondVarType cv_tx;
  CondVarType cv_rx;

  bool dynamic; // true=>dynamically-sized buffer

  struct QueData
  {
    AVFrame *frame;
    bool eof;       // true if eof (frame is unreferenced)
    bool populated; // true if data is available
  };

  std::vector<QueData> que;                   // queue containing
  typename std::vector<QueData>::iterator wr; // points to next to be written
  typename std::vector<QueData>::iterator rd; // points to next to be read
};

typedef AVFrameQueue<NullMutex, NullConditionVariable<NullMutex>,
                     NullUniqueLock<NullMutex>>
    AVFrameQueueST;

} // namespace ffmpeg
