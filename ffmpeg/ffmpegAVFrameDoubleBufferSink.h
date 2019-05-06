#pragma once

#include "ffmpegAVFrameBufferBases.h"

extern "C" {
#include <libavutil/frame.h>
}

#include <chrono>

namespace ffmpeg
{
/**
 * \brief   Template AVFrame sink to manage receive & send buffers
 */
template <typename Buffer, class Mutex_t = std::shared_mutex>
class AVFrameDoubleBufferSink : public AVFrameSinkBase<Mutex_t>
{
protected:
  /**
 * \brief Constructor
 * 
 * Passes all the input parameters to the internal buffers.
 */
  template <class... Args>
  AVFrameDoubleBufferSink(Args... args)
  {
    // create 2 buffers
    buffers.emplace_back(args...);
    buffers.emplace_back(args...);

    // copy the buffer media type
    type = buffers[0].getMediaType();

    // initilaize receiver & sender
    receiver = buffers.begin();
    sender = buffers.end(); // data not available
  };

  /**
   * \brief Copy constructor
   */
  AVFrameDoubleBufferSink(const AVFrameDoubleBufferSink &src) : AVFrameSinkBase(src)
  {
    buffers.emplace_back(src.buffers[0]);
    buffers.emplace_back(src.buffers[1]);
    if (src.receiver == src.buffers.end())
      receiver = buffers.end();
    else
      receiver = buffers.begin() + (src.receiver != src.buffers.begin());

    if (src.sender == src.buffers.end())
      sender = buffers.end();
    else
      sender = buffers.begin() + (src.sender != src.buffers.begin());
  }

  /**
   * \brief Move constructor
   */
  AVFrameDoubleBufferSink(AVFrameDoubleBufferSink &&src) : AVFrameSinkBase(src)
  {
    buffers = std::move(src.buffers);
    receiver = std::move(src.receiver);
    sender = std::move(src.sender);
  }

public:
  virtual ~AVFrameDoubleBufferSink(){};

  /**
   * \brief Receive AVFrame, block if no space avail
   */
  virtual void push(AVFrame *frame)
  {
    // read-lock to check if there is room left in the current receive buffer
    // if not, upgrade to write-lock to swap buffers
    //         if send buffer not read yet, wait
    //         downgrade the lock
    // then perform receiver's push 

    std::shared_lock<MUTEX_t> l_rx(m);
    if (!readyToPush_threadunsafe())
    {
      std::unique_lock<Mutex_t> ul_rx = upgrade_lock(l_rx);
      swap_buffer();
      l_rx.unlock();
      cv_rx.wait(l_rx);
    }
    push_threadunsafe(frame);

    receiver.tryToPush(frame);
  }

  /**
   * \brief Receive AVFrame, block if no space avail
   */
  template <class Predicate>
  virtual int push(AVFrame *frame, Predicate pred)
  {
    std::shared_lock<MUTEX_t> l_rx(m);
    bool ready = true;
    while (ready && !readyToPush_threadunsafe())
      ready = cv_rx.wait(l_rx, pred);
    if (!ready)
      return AVERROR(EAGAIN);

    push_threadunsafe(frame);
    return 0;
  }

  /**
   * \brief Receive AVFrame, block if no space avail
   */
  virtual int push(AVFrame *frame, const std::chrono::milliseconds &rel_time)
  {
    std::shared_lock<MUTEX_t> l_rx(m);
    std::cv_status status = std::cv_status::no_timeout;
    while (status == std::cv_status::no_timeout && !readyToPush_threadunsafe())
      status = cv_rx.wait_for(l_rx, rel_time);
    if (status == std::cv_status::timeout)
      return AVERROR(EAGAIN);

    push_threadunsafe(frame);
    return 0;
  }

  /**
   * \brief Receive AVFrame, block if no space avail
   */
  template <class Predicate>
  virtual int push(AVFrame *frame, const std::chrono::milliseconds &rel_time, Predicate pred)
  {
    std::shared_lock<MUTEX_t> l_rx(m);
    bool ready = true;
    while (ready && !readyToPush_threadunsafe())
      ready = cv_rx.wait_for(m, rel_time, pred);
    if (!ready)
      return AVERROR(EAGAIN);

    push_threadunsafe(frame);
    return 0;
  }

  /**
   * \brief Try to receive AVFrame only without blocking
   */
  virtual int tryToPush(AVFrame *frame)
  {
    std::shared_lock<MUTEX_t> l_rx(m);
    if (!readyToPush_threadunsafe())
      return AVERROR(EAGAIN);

    push_threadunsafe(frame);
    return 0;
  }

  /**
   * \brief Perform a task on sender buffer
   * 
   * \param[in] op Function to process the sender buffer. Must have the signature
   *            \code{c++}
   *            bool op(Buffer &sender);
   *            \endcode
   *            The function shall return true if the sender buffer can be released
   *
   * \note Supports simultaneous accesses from multiple threads with shared_mutex
   * 
   */
  template<ReadOp>
  bool processSenderBuffer(ReadOp op)
  {
    std::shared_lock<Mutex_t> l_rx(m);

    if (sender==buffers.end())

    if (op(*sender))
  }

  /**
   * \brief Perform the same operation on both buffers
   * 
   * \note Thread unsafe
   */
  template <typename BufferOp>
  void forEachBuffer(BufferOp op)
  {
    op(buffers[0]);
    op(buffers[1]);
  }

  /**
   * \brief Perform the same const operation on both buffers
   * 
   * \note Thread unsafe
   */
  template <typename BufferConstOp>
  void forEachBufferConst(BufferOp op) const
  {
    op(buffers[0]);
    op(buffers[1]);
  }

  /**
   * \brief True if given FFmpeg format is valid
   */
  bool supportedFormat(int format) const
  {
    return buffers[0].supportedFormat(format);
  }

protected:
  virtual bool readyToPush_threadunsafe() const
  {
    return receiver.readyToPush();
  }
  virtual int push_threadunsafe(AVFrame *frame)
  {
    // receive the frame
    int rval = receiver.push(frame);

    // if receiver is full, swap

  }
  virtual bool clear_threadunsafe(bool deep = false)
  {
    bool rval = buffers[0].clear(deep);
    rval = buffers[1].clear(deep);
  }

  virtual bool eof_threadunsafe() const
  {
    bool sender.eof();
  }

private:
  typedef std::vector<Buffer> Buffers;
  Buffers buffers;
  Buffers::iterator receiver; // receives new AVFrame
  Buffers::iterator sender;    // sends new stored AVFrame data
};
