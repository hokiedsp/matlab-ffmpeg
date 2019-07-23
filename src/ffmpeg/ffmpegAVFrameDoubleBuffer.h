#pragma once

#include "ffmpegAVFrameQueue.h"

#include <algorithm>

namespace ffmpeg
{

/**
 * \brief   Template AVFrame sink to manage receive & send buffers
 */
template <typename MutexType, typename CondVarType, typename MutexLockType>
class AVFrameDoubleBuffer : public IAVFrameBuffer
{
  public:
  AVFrameDoubleBuffer(size_t N = 0);
  AVFrameDoubleBuffer(const AVFrameDoubleBuffer &src) = delete;
  ~AVFrameDoubleBuffer() {}

  AVFrameDoubleBuffer &operator=(const AVFrameDoubleBuffer &src) = delete;

  bool ready() const { return true; }

  bool autoexpand() const { return rcvr->autoexpand(); }

  IAVFrameSource &getSrc() const { return rcvr->getSrc(); }
  void setSrc(IAVFrameSource &src);
  void clrSrc();

  IAVFrameSink &getDst() const { return sndr->getDst(); }
  void setDst(IAVFrameSink &dst);
  void clrDst();

  const MediaParams &getMediaParams() const;

  void clear();
  size_t size() noexcept;
  bool empty() noexcept;
  bool full() noexcept;

  bool readyToPush();
  void blockTillReadyToPush();
  bool blockTillReadyToPush(const std::chrono::milliseconds &timeout_duration);
  AVFrame *peekToPush();
  void push();
  void push(AVFrame *frame);
  bool push(AVFrame *frame, const std::chrono::milliseconds &timeout_duration);
  bool tryToPush(AVFrame *frame);

  bool readyToPop();
  void blockTillReadyToPop();
  bool blockTillReadyToPop(const std::chrono::milliseconds &timeout_duration);
  AVFrame *peekToPop();
  void pop();
  void pop(AVFrame *frame, bool *eof = nullptr);
  bool pop(AVFrame *frame, bool *eof,
           const std::chrono::milliseconds &timeout_duration);
  bool tryToPop(AVFrame *frame, bool *eof = nullptr);

  bool eof();
  bool eof(const std::chrono::milliseconds &rel_time);

  /**
   * \brief swaps rcvr & sndr buffers
   *
   * \note waits
   */
  void swap();

  private:
  bool readyToPush_threadunsafe();
  bool readyToPop_threadunsafe();

  template <typename Action>
  void push_threadunsafe(MutexLockType &lock, AVFrame *frame, Action action);

  template <typename Action>
  void pop_threadunsafe(MutexLockType &lock, AVFrame *frame, bool *eof,
                        Action action);

  void swap_threadunsafe() { std::swap(rcvr, sndr); }

  typedef std::vector<AVFrameQueueST> Buffers;
  Buffers buffers;
  Buffers::iterator rcvr; // receives new AVFrame
  Buffers::iterator sndr; // sends new stored AVFrame data

  MutexType mutex;
  CondVarType cv_tx;
  CondVarType cv_rx;
  CondVarType cv_swap;
  bool swappable; // true to block buffer swappage
};

typedef AVFrameDoubleBuffer<Cpp11Mutex, Cpp11ConditionVariable,
                            Cpp11UniqueLock<Cpp11Mutex>>
    AVFrameDoubleBufferMT;

/// IMPLEMENTATION ///

template <typename MutexType, typename CondVarType, typename MutexLockType>
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::AVFrameDoubleBuffer(
    size_t N)
    : swappable(true)
{
  // set up a double buffer
  buffers.reserve(2);
  buffers.emplace_back(N);
  buffers.emplace_back(N);

  // assign writer & reader
  rcvr = buffers.begin();
  sndr = rcvr + 1;
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::setSrc(
    IAVFrameSource &src)
{
  for (auto &buf : buffers) buf.setSrc(src);
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::clrSrc()
{
  for (auto &buf : buffers) buf.clrSrc();
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::setDst(
    IAVFrameSink &dst)
{
  for (auto &buf : buffers) buf.setDst(dst);
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::clrDst()
{
  for (auto &buf : buffers) buf.clrDst();
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline const MediaParams &
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::getMediaParams()
    const
{
  return rcvr->getMediaParams();
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::clear()
{
  MutexLockType lock(mutex);
  for (auto &buf : buffers) buf.clear();
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline size_t
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::size() noexcept
{
  MutexLockType lock(mutex);
  return std::reduce(buffers.begin(), buffers.end(), 0ull,
                     [](auto cnt, auto &buf) { return cnt + buf.size(); });
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::empty() noexcept
{
  MutexLockType lock(mutex);
  return std::all_of(buffers.begin(), buffers.end(),
                     [](auto &buf) { return buf.empty(); });
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::full() noexcept
{
  MutexLockType lock(mutex);
  return std::all_of(buffers.begin(), buffers.end(),
                     [](auto &buf) { return buf.full(); });
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::readyToPush()
{
  MutexLockType lock(mutex);
  return readyToPush_threadunsafe();
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType,
                                MutexLockType>::blockTillReadyToPush()
{
  MutexLockType lock(mutex);
  cv_rx.wait(lock, [this] { return readyToPush_threadunsafe(); });
  return;
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::
    blockTillReadyToPush(const std::chrono::milliseconds &timeout_duration)
{
  MutexLockType lock(mutex);
  return cv_rx.wait_for(lock, timeout_duration,
                        [this] { return readyToPush_threadunsafe(); });
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType,
                                MutexLockType>::readyToPush_threadunsafe()
{
  return !rcvr->full() || (swappable && sndr->empty());
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline AVFrame *
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::peekToPush()
{
  MutexLockType lock(mutex);
  cv_rx.wait(lock, [this] { return readyToPush_threadunsafe(); });
  if (rcvr->full()) swap_threadunsafe(); // gets here only if swappable is true
  swappable = false;
  lock.unlock();
  auto frame = rcvr->peekToPush();
  return frame;
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::push()
{
  MutexLockType lock(mutex);
  cv_rx.wait(lock, [this] { return readyToPush_threadunsafe(); });
  push_threadunsafe(lock, nullptr,
                    [](auto &rcvr, AVFrame *frame) { rcvr->push(); });
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::push(AVFrame *frame)
{
  MutexLockType lock(mutex);
  cv_rx.wait(lock, [this] { return readyToPush_threadunsafe(); });
  push_threadunsafe(lock, frame,
                    [](auto &rcvr, AVFrame *frame) { rcvr->push(frame); });
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::push(
    AVFrame *frame, const std::chrono::milliseconds &timeout_duration)
{
  MutexLockType lock(mutex);
  bool success = cv_rx.wait_for(lock, timeout_duration,
                                [this] { return readyToPush_threadunsafe(); });
  if (success)
    push_threadunsafe(lock, frame,
                      [](auto &rcvr, AVFrame *frame) { rcvr->push(frame); });
  return success;
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
template <typename Action>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::push_threadunsafe(
    MutexLockType &lock, AVFrame *frame, Action push)
{
  if (rcvr->full()) swap_threadunsafe(); // gets here only if swappable is true
  swappable = false;
  lock.unlock(); // momentarily release mutex to allow other thread to pop
  push(rcvr, frame);
  lock.lock();
  swappable = true;
  cv_tx.notify_one();
  cv_swap.notify_one();
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::tryToPush(
    AVFrame *frame)
{
  MutexLockType lock(mutex);
  if (!readyToPush_threadunsafe()) return false;

  push_threadunsafe(lock, frame,
                    [](auto &rcvr, AVFrame *frame) { rcvr->push(frame); });
  return true;
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::readyToPop()
{
  MutexLockType lock(mutex);
  return readyToPop_threadunsafe();
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType,
                                MutexLockType>::readyToPop_threadunsafe()
{
  return sndr->size() || (swappable && (rcvr->full() || rcvr->eof() ||
                                        (rcvr->autoexpand() && rcvr->size())));
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType,
                                MutexLockType>::blockTillReadyToPop()
{
  MutexLockType lock(mutex);
  cv_tx.wait(lock, [this] { return readyToPop_threadunsafe(); });
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::blockTillReadyToPop(
    const std::chrono::milliseconds &timeout_duration)
{
  MutexLockType lock(mutex);
  return cv_tx.wait_for(lock, timeout_duration,
                        [this] { return readyToPop_threadunsafe(); });
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline AVFrame *
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::peekToPop()
{
  MutexLockType lock(mutex);
  cv_tx.wait(lock, [this] { return readyToPop_threadunsafe(); });
  if (sndr->empty()) swap_threadunsafe();
  return sndr->peekToPop();
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::pop()
{
  MutexLockType lock(mutex);
  cv_tx.wait(lock);
  pop_threadunsafe(lock, nullptr, nullptr,
                   [](auto &sndr, AVFrame *frame, bool *eof) { sndr->pop(); });
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::pop(AVFrame *frame,
                                                                bool *eof)
{
  MutexLockType lock(mutex);
  cv_tx.wait(lock);
  pop_threadunsafe(lock, frame, eof, [](auto &sndr, AVFrame *frame, bool *eof) {
    return sndr->pop(frame, eof);
  });
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::pop(
    AVFrame *frame, bool *eof,
    const std::chrono::milliseconds &timeout_duration)
{
  if (!frame) throw Exception("frame must be non-null pointer.");
  MutexLockType lock(mutex);
  cv_tx.wait(lock, [this] { return readyToPop_threadunsafe(); });
  bool success = cv_tx.wait_for(lock, timeout_duration,
                                [this] { return readyToPop_threadunsafe(); });
  if (success)
    pop_threadunsafe(
        lock, frame, eof,
        [](auto &sndr, AVFrame *frame, bool *eof) { sndr->pop(frame, eof); });
  return success;
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
template <typename Action>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::pop_threadunsafe(
    MutexLockType &lock, AVFrame *frame, bool *eof, Action pop)
{
  if (sndr->empty()) swap_threadunsafe(); // gets here only if swappable is true
  swappable = false;
  lock.unlock(); // momentarily release mutex to allow other thread to pop
  pop(sndr, frame, eof);
  lock.lock();
  swappable = true;
  cv_rx.notify_one();
  cv_swap.notify_one();
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::tryToPop(
    AVFrame *frame, bool *eof)
{
  MutexLockType lock(mutex);
  if (!readyToPop_threadunsafe()) return false;

  pop_threadunsafe(lock, frame, eof, [](auto &sndr, AVFrame *frame, bool *eof) {
    sndr->pop(frame, eof);
  });
  return true;
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::eof()
{
  MutexLockType lock(mutex);
  cv_tx.wait(lock, [this] { return readyToPop_threadunsafe(); });
  if (sndr->empty()) swap_threadunsafe(); // gets here only if swappable is true
  return sndr->eof();
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::eof(
    const std::chrono::milliseconds &rel_time)
{
  MutexLockType lock(mutex);
  if (!cv_tx.wait_for(lock, rel_time,
                      [this] { return readyToPop_threadunsafe(); }))
    throw Exception("Timed out while waiting to check for eof.");
  if (sndr->empty()) swap_threadunsafe(); // gets here only if swappable is true
  return sndr->eof();
}

template <typename MutexType, typename CondVarType, typename MutexLockType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType>::swap()
{
  MutexLockType lock(mutex);
  swap_threadunsafe();
}

} // namespace ffmpeg
