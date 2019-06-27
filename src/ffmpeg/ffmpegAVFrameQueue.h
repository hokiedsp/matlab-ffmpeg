#pragma once

#include "ffmpegAVFrameBufferInterfaces.h"

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
    // insert the first frame
    expand();
    if (!dynamic) // if fixed queue size, expand to the desired size
      for (int i = 1; i < N; ++i) expand();

    // set read/write pointers to the beginning
    wr = rd = que.begin();
  } // queue size

  virtual ~AVFrameQueue()
  {
    // release allocated memory for all the AVFrames
    for (auto it = que.begin(); it != que.end(); ++it) av_frame_free(&(it->frame));
  }

  const MediaParams &getMediaParams() const
  {
    if (src) return src->getMediaParams();
    throw Exception("Media parameters could be retrieved only if src is connected.");
  }

  IAVFrameSource &getSrc() const { return *src; };
  void setSrc(IAVFrameSource &buf) { src = &buf; }
  void clrSrc() { src = nullptr; }

  IAVFrameSink &getDst() const { return *dst; };
  void setDst(IAVFrameSink &buf) { dst = &buf; }
  void clrDst() { dst = nullptr; }

  bool ready() const { return src && dst; };

  void clear()
  {
    MutexLockType lock(mutex);
    for (auto it = que.begin(); it != que.end(); ++it)
    {
      if (it->populated)
      {
        av_frame_unref(it->frame);
        it->populated = false;
      }
    }
  }

  size_t size() const noexcept
  {
    MutexLockType lock(mutex);
    return (wr - rd) + (wr < rd) ? que.size() : 0;
  }
  bool empty() const noexcept
  {
    MutexLockType lock(mutex);
    return (wr != que.begin()) ? (wr - 1)->populated : que.back().populated;
  }
  bool full() const noexcept
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
    return cv_rx.wait_for(lock, rel_time, [this] { return readyToPush_threadunsafe(); });
  }

  AVFrame *peekToPush()
  {
    MutexLockType lock(mutex);
    cv_rx.wait(lock, [this] { return readyToPush_threadunsafe(); });
    if (wr->populated) throw_or_expand(); // expand if allowed or throws overflow exception
    return wr->frame;
  }

  void push()
  {
    MutexLockType lock(mutex);
    cv_rx.wait(lock);
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
    bool success = cv_rx.wait_for(lock, rel_time, [this] { return readyToPush_threadunsafe(); });
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
    return cv_tx.wait_for(lock, rel_time, [this] { return readyToPop_threadunsafe(); });
  }

  void pop(AVFrame *frame, bool &eof)
  {
    if (!frame) throw Exception("frame must be non-null pointer.");
    MutexLockType lock(mutex);
    cv_tx.wait(lock, [this] { return readyToPop_threadunsafe(); });
    eof = pop_threadunsafe(frame);
  }

  bool pop(AVFrame *frame, bool &eof, const std::chrono::milliseconds &rel_time)
  {
    if (!frame) throw Exception("frame must be non-null pointer.");
    MutexLockType lock(mutex);
    bool success = cv_tx.wait_for(lock, rel_time, [this] { return readyToPop_threadunsafe(); });
    if (success) eof = pop_threadunsafe(frame);
    return success;
  }

  void pop_back(AVFrame *frame, bool &eof)
  {
    if (!frame) throw Exception("frame must be non-null pointer.");
    MutexLockType lock(mutex);
    cv_tx.wait(lock, [this] { return readyToPop_threadunsafe(); });

    // pop the last written AVFrame, use with caution
    auto last = (wr == que.begin()) ? (que.end() - 1) : (wr - 1);
    if (!last->populated) throw Exception("No frame available.");

    // increment the read pointer and get the frame
    eof = last->eof;
    if (!eof) av_frame_move_ref(frame, last->frame);
    last->populated = false;
    wr = last;
  }

  bool eof()
  {
    MutexLockType lock(mutex);
    cv_tx.wait(lock, [this] { return readyToPop_threadunsafe(); });
    return rd->eof;
  }

  bool eof(const std::chrono::milliseconds &rel_time)
  {
    MutexLockType lock(mutex);
    if (!cv_tx.wait_for(lock, rel_time, [this] { return readyToPop_threadunsafe(); }))
      throw Exception("Timed out while waiting to check for eof.");
    return rd->eof; // true if no more frames in the buffer
  }

  bool tryToPop(AVFrame *frame, bool &eof)
  {
    MutexLockType lock(mutex);
    if (readyToPop_threadunsafe())
    {
      eof = pop_threadunsafe(frame);
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
    pop_threadunsafe(nullptr);
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

    if (wr == que.end() - 1)
      que.push_back({av_frame_alloc(), false, false});
    else
      que.insert(wr, {av_frame_alloc(), false, false});

    wr = que.begin() + Iwr;
    rd = que.begin() + Ird;
  }

  bool readyToPush_threadunsafe() // declared in AVFrameSinkBase
  {
    return dynamic || !wr->populated;
  }
  bool readyToPop_threadunsafe() // declared in AVFrameSourceBase
  {
    return rd->populated;
  }
  void push_threadunsafe(AVFrame *frame) // declared in AVFrameSinkBase
  {
    // if buffer is not available
    if (wr->populated)
      throw_or_expand(); // expand if allowed or throws overflow exception

    // copy the frame data
    if (frame) av_frame_ref(wr->frame, frame);
    wr->eof = !frame;

    // set the written flag and increment write iterator
    (wr++)->populated = true;
    if (wr == que.end()) wr = que.begin();

    // notify the source-end for the arrival of new data
    cv_tx.notify_one();
  }

  void mark_populated_threadunsafe() // declared in AVFrameSinkBase
  {
    // if buffer is not available
    if (wr->populated) throw Exception("Already populated.");

    // set the written flag and increment write iterator
    (wr++)->populated = true;
    if (wr == que.end()) wr = que.begin();

    // notify the source-end for the arrival of new data
    cv_tx.notify_one();
  }

  bool pop_threadunsafe(AVFrame *frame) // declared in AVFrameSourceBase
  {
    // guaranteed readyToPop() returns true

    // increment the read pointer then
    bool eof = (++rd)->eof;

    // get the frame
    if (!eof)
    {
      if (frame)
        av_frame_move_ref(frame, rd->frame);
      else
        av_frame_unref(rd->frame); // just in case
      rd->populated = false;
    }

    // notify the sink-end for slot opening
    cv_rx.notify_one();

    return eof;
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

} // namespace ffmpeg
