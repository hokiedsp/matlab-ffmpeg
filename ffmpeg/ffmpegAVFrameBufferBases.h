#pragma once

#include "ffmpegAVFrameBufferInterfaces.h"
#include "ffmpegMediaStructs.h"

#include <mutex>
#include <shared_mutex>
#include <condition_variable>

namespace ffmpeg
{
// Base Thread-Safe Buffer Class
template <class Mutex_t = std::mutex>
class AVFrameBufferBase : public MediaHandler
{
public:
  virtual ~AVFrameBufferBase()
  {
    av_log(NULL, AV_LOG_INFO, "destroying AVFrameBufferBase\n");
  }

protected:
  // default constructor shall only be called implicitly while consturucting its virtually inheriting derived classes
  AVFrameBufferBase()
  {
    av_log(NULL, AV_LOG_INFO, "[AVFrameBufferBase:default] default constructor\n");
  }

  AVFrameBufferBase(const AVMediaType mediatype, const AVRational &tb) : MediaHandler(mediatype, tb)
  {
    av_log(NULL, AV_LOG_INFO, "[AVFrameBufferBase:regular] mediatype:%s->%s :: time_base:%d/%d->%d/%d\n",
           av_get_media_type_string(mediatype), av_get_media_type_string(type), tb.num, tb.den, time_base.num, time_base.den);
  }

  // copy constructor
  AVFrameBufferBase(const AVFrameBufferBase &other) : MediaHandler(other) {}

  // move constructor
  AVFrameBufferBase(AVFrameBufferBase &&other) noexcept : MediaHandler(other) {}

protected:
  Mutex_t m; // mutex to access the buffer
};

///////////////////////////////////////////////////////////////////////////////////////////////////

// Base Thread-Safe Sink Class
template <class Mutex_t = std::shared_mutex>
class AVFrameSinkBase : public IAVFrameSink, public AVFrameBufferBase<Mutex_t>
{
protected:
  AVFrameSinkBase(){}; // must be default-constructable

  AVFrameSinkBase(const AVMediaType mediatype, const AVRational &tb)
  {
    // since it could be inherited virtually, default-construct base classes then
    // set the parameters after its base classes are constructed
    type = mediatype;
    time_base = tb;
  }

public:
  virtual ~AVFrameSinkBase(){};

  // copy constructor
  AVFrameSinkBase(const AVFrameSinkBase &other) : AVFrameBufferBase(other) {}

  // move constructor
  AVFrameSinkBase(AVFrameSinkBase &&other) noexcept : AVFrameBufferBase(other) {}

  virtual bool ready() const
  {
    return true;
  }

  virtual bool eof()
  {
    std::shared_lock<Mutex_t> l_rx(m);
    return eof_threadunsafe();
  }

  virtual void clear(const bool deep = false)
  {
    std::unique_lock<Mutex_t> l_rx(m);
    if (clear_threadunsafe(deep))
      cv_rx.notify_one();
  }

  virtual bool readyToPush()
  {
    std::shared_lock<Mutex_t> l_rx(m);
    return readyToPush_threadunsafe();
  }

  virtual void blockTillReadyToPush()
  {
    std::shared_lock<Mutex_t> l_rx(m);
    while (!readyToPush_threadunsafe())
      cv_rx.wait(l_rx);
  }
  virtual bool blockTillReadyToPush(const std::chrono::milliseconds &rel_time)
  {
    std::shared_lock<Mutex_t> l_rx(m);
    std::cv_status status = std::cv_status::no_timeout;
    while (status == std::cv_status::no_timeout && !readyToPush_threadunsafe())
      status = cv_rx.wait_for(l_rx, rel_time);
    return status == std::cv_status::no_timeout;
  }

  virtual int tryToPush(AVFrame *frame)
  {
    std::unique_lock<Mutex_t> l_rx(m);
    if (!readyToPush_threadunsafe())
      return AVERROR(EAGAIN);

    push_threadunsafe(frame);
    return 0;
  }

  virtual void push(AVFrame *frame)
  {
    std::unique_lock<Mutex_t> l_rx(m);
    while (!readyToPush_threadunsafe())
      cv_rx.wait(l_rx);
    push_threadunsafe(frame);
  }

  template <class Predicate>
  virtual int push(AVFrame *frame, Predicate pred)
  {
    std::unique_lock<Mutex_t> l_rx(m);
    bool ready = true;
    while (ready && !readyToPush_threadunsafe())
      ready = cv_rx.wait(l_rx, pred);
    if (!ready)
      return AVERROR(EAGAIN);

    push_threadunsafe(frame);
    return 0;
  }

  virtual int push(AVFrame *frame, const std::chrono::milliseconds &rel_time)
  {
    std::unique_lock<Mutex_t> l_rx(m);
    std::cv_status status = std::cv_status::no_timeout;
    while (status == std::cv_status::no_timeout && !readyToPush_threadunsafe())
      status = cv_rx.wait_for(l_rx, rel_time);
    if (status == std::cv_status::timeout)
      return AVERROR(EAGAIN);

    push_threadunsafe(frame);
    return 0;
  }

  template <class Predicate>
  virtual int push(AVFrame *frame, const std::chrono::milliseconds &rel_time, Predicate pred)
  {
    std::unique_lock<Mutex_t> l_rx(m);
    bool ready = true;
    while (ready && !readyToPush_threadunsafe())
      ready = cv_rx.wait_for(m, rel_time, pred);
    if (!ready)
      return AVERROR(EAGAIN);

    push_threadunsafe(frame);
    return 0;
  }

protected:
  virtual bool readyToPush_threadunsafe() const = 0;
  virtual int push_threadunsafe(AVFrame *frame) = 0;
  virtual bool clear_threadunsafe(bool deep = false) = 0;

  /**
   * \brief   Mutex lock transfer from shared to unique
   * 
   * \note Another thread may uniquely lock the mutex before the calling
   *       thread re-obtains the lock
   */
  std::unique_lock<Mutex_t> upgrade_lock(std::shared_lock<Mutex_t> &s_lk)
  {
    s_lk.unlock();
    return std::unique_lock<Mutex_t>(*s_lk.release());
  }

  /**
   * \brief   Mutex lock transfer from unique to shared
   * 
   * \note Another thread may uniquely lock the mutex before the calling
   *       thread re-obtains the lock
   */
  std::shared_lock<Mutex_t> downgrade_lock(std::unique_lock<Mutex_t> &u_lk)
  {
    u_lk.unlock();
    return std::shared_lock<Mutex_t>(*u_lk.release());
  }

  std::condition_variable_any cv_rx;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

// Base Source Class
template <class Mutex_t = std::mutex>
class AVFrameSourceBase : public AVFrameBufferBase<Mutex_t>, public IAVFrameSource
{
public:
  virtual ~AVFrameSourceBase(){};

protected:
  AVFrameSourceBase(const AVMediaType mediatype, const AVRational &tb) : AVFrameBufferBase(mediatype, tb)
  {
    av_log(NULL, AV_LOG_INFO, "[AVFrameSourceBase:default] time_base:%d/%d->%d/%d\n", tb.num, tb.den, time_base.num, time_base.den);
    av_log(NULL, AV_LOG_INFO, "[AVFrameSourceBase:default] mediatype:%s->%s\n", av_get_media_type_string(mediatype), av_get_media_type_string(type));
  }
  // copy constructor
  AVFrameSourceBase(const AVFrameSourceBase &other) : AVFrameBufferBase(other) {}

  // move constructor
  AVFrameSourceBase(AVFrameSourceBase &&other) noexcept : AVFrameBufferBase(other) {}

public:
  using AVFrameBufferBase::ready;

  virtual void clear()
  {
    std::unique_lock<Mutex_t> l_tx(m);
    clear_threadunsafe();
  }

  int tryToPop(AVFrame *frame)
  {
    std::unique_lock<Mutex_t> l_tx(m);
    if (readyToPop_threadunsafe())
    {
      pop_threadunsafe(frame);
      return 0;
    }
    else
    {
      frame = NULL;
      return AVERROR(EAGAIN);
    }
  }

  void pop(AVFrame *frame)
  {
    std::unique_lock<Mutex_t> l_tx(m);
    while (!readyToPop_threadunsafe())
      cv_tx.wait(l_tx);
    pop_threadunsafe(frame);
  }

  template <class Predicate>
  int pop(AVFrame *frame, Predicate pred)
  {
    std::unique_lock<Mutex_t> l_tx(m);
    bool ready = true;
    while (ready && !readyToPop_threadunsafe())
      ready = cv_rx.wait(l_tx, pred);

    if (ready)
    {
      pop_threadunsafe(frame);
      return 0;
    }
    else
    {
      frame = NULL;
      return AVERROR(EAGAIN);
    }
  }

  int pop(AVFrame *frame, const std::chrono::milliseconds &rel_time)
  {
    std::unique_lock<Mutex_t> l_tx(m);
    std::cv_status status = std::cv_status::no_timeout;
    while (status == std::cv_status::no_timeout && !readyToPop_threadunsafe())
      status = cv_tx.wait_for(l_tx, rel_time);
    if (status == std::cv_status::no_timeout)
    {
      pop_threadunsafe(frame);
      return 0;
    }
    else
    {
      frame = NULL;
      return AVERROR(EAGAIN);
    }
  }

  template <class Predicate>
  int pop(AVFrame *frame, const std::chrono::milliseconds &rel_time, Predicate pred)
  {
    std::unique_lock<Mutex_t> l_tx(m);
    bool ready = true;
    while (ready && !readyToPop_threadunsafe())
      ready = cv_tx.wait_for(m, rel_time, pred);
    if (ready)
    {
      pop_threadunsafe(frame);
      return 0;
    }
    else
    {
      frame = NULL;
      return AVERROR(EAGAIN);
    }
  }

  bool readyToPop()
  {
    std::unique_lock<Mutex_t> l_rx(m);
    return readyToPop_threadunsafe();
  }

  void blockTillReadyToPop()
  {
    std::unique_lock<Mutex_t> l_tx(m);
    while (!readyToPop_threadunsafe())
      cv_tx.wait(l_tx);
  }

  template <typename Pred>
  bool blockTillReadyToPop(Pred pred)
  {
    std::unique_lock<Mutex_t> l_tx(m);
    bool ready = true;
    while (ready && !readyToPop_threadunsafe())
      ready = cv_rx.wait(l_tx, pred);
    return ready;
  }

  bool blockTillReadyToPop(const std::chrono::milliseconds &rel_time)
  {
    std::unique_lock<Mutex_t> l_tx(m);
    std::cv_status status = std::cv_status::no_timeout;
    while (status == std::cv_status::no_timeout && !readyToPop_threadunsafe())
      status = cv_tx.wait_for(l_tx, rel_time);
    return status == std::cv_status::no_timeout;
  }

  template <typename Pred>
  void blockTillReadyToPop(const std::chrono::milliseconds &rel_time, Pred pred)
  {
    std::unique_lock<Mutex_t> l_tx(m);
    bool ready = true;
    while (ready && !readyToPop_threadunsafe())
      ready = cv_rx.wait_for(l_tx, rel_time, pred);
    return ready;
  }

  virtual bool ready() const
  {
    return time_base.num > 0 && time_base.den > 0;
  }

protected:
  virtual bool readyToPop_threadunsafe() const = 0;
  virtual void pop_threadunsafe(AVFrame *frame) = 0;
  virtual void clear_threadunsafe() = 0;
  virtual bool eof_threadunsafe() const = 0;

  std::condition_variable cv_tx;
};
}
