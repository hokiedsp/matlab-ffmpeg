#pragma once

#include "ffmpegAVFrameBufferInterfaces.h"
#include "ffmpegMediaStructs.h"

#include <mutex>
#include <condition_variable>

namespace ffmpeg
{
// Base Thread-Safe Buffer Class
class AVFrameBufferBase : public MediaHandler
{
public:
  virtual ~AVFrameBufferBase() {}

protected:
  // default constructor shall only be called implicitly while consturcting its virtually inheriting derived classes
  AVFrameBufferBase()
  {
    av_log(NULL, AV_LOG_INFO, "[AVFrameBufferBase:default] default constructor\n");
  }

  AVFrameBufferBase(const AVMediaType mediatype, const AVRational &tb)
  {
    av_log(NULL, AV_LOG_INFO, "[AVFrameBufferBase:regular] mediatype:%s->%s :: time_base:%d/%d->%d/%d\n",
           av_get_media_type_string(mediatype), av_get_media_type_string(type), tb.num, tb.den, time_base.num, time_base.den);
  }

  // copy constructor
  AVFrameBufferBase(const AVFrameBufferBase &other) : MediaHandler(other) {}

  // move constructor
  AVFrameBufferBase(AVFrameBufferBase &&other) noexcept : MediaHandler(other) {}

protected:
  std::mutex m; // mutex to access the buffer
};

///////////////////////////////////////////////////////////////////////////////////////////////////

// Base Thread-Safe Sink Class
class AVFrameSinkBase : public IAVFrameSink,
                        public AVFrameBufferBase
{
protected:
  AVFrameSinkBase() {}; // must be default-constructable

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

  virtual void clear(const bool deep = false)
  {
    std::unique_lock<std::mutex> l_rx(m);
    if (clear_threadunsafe(deep))
      cv_rx.notify_one();
  }

  bool readyToPush()
  {
    std::unique_lock<std::mutex> l_rx(m);
    return readyToPush_threadunsafe();
  }

  void blockTillReadyToPush()
  {
    std::unique_lock<std::mutex> l_rx(m);
    while (!readyToPush_threadunsafe())
      cv_rx.wait(l_rx);
  }
  bool blockTillReadyToPush(const std::chrono::milliseconds &rel_time)
  {
    std::unique_lock<std::mutex> l_rx(m);
    std::cv_status status = std::cv_status::no_timeout;
    while (status == std::cv_status::no_timeout && !readyToPush_threadunsafe())
      status = cv_rx.wait_for(l_rx, rel_time);
    return status == std::cv_status::no_timeout;
  }

  int tryToPush(AVFrame *frame)
  {
    std::unique_lock<std::mutex> l_rx(m);
    if (!readyToPush_threadunsafe())
      return AVERROR(EAGAIN);

    push_threadunsafe(frame);
    return 0;
  }

  void push(AVFrame *frame)
  {
    std::unique_lock<std::mutex> l_rx(m);
    while (!readyToPush_threadunsafe())
      cv_rx.wait(l_rx);
    push_threadunsafe(frame);
  }

  template <class Predicate>
  int push(AVFrame *frame, Predicate pred)
  {
    std::unique_lock<std::mutex> l_rx(m);
    bool ready = true;
    while (ready && !readyToPush_threadunsafe())
      ready = cv_rx.wait(l_rx, pred);
    if (!ready)
      return AVERROR(EAGAIN);

    push_threadunsafe(frame);
    return 0;
  }

  int push(AVFrame *frame, const std::chrono::milliseconds &rel_time)
  {
    std::unique_lock<std::mutex> l_rx(m);
    std::cv_status status = std::cv_status::no_timeout;
    while (status == std::cv_status::no_timeout && !readyToPush_threadunsafe())
      status = cv_rx.wait_for(l_rx, rel_time);
    if (status == std::cv_status::timeout)
      return AVERROR(EAGAIN);

    push_threadunsafe(frame);
    return 0;
  }

  template <class Predicate>
  int push(AVFrame *frame, const std::chrono::milliseconds &rel_time, Predicate pred)
  {
    std::unique_lock<std::mutex> l_rx(m);
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

  std::condition_variable cv_rx;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

// Base Source Class
class AVFrameSourceBase : public AVFrameBufferBase, public IAVFrameSource
{
public:
  virtual ~AVFrameSourceBase(){};

protected:
  AVFrameSourceBase(const AVMediaType mediatype, const AVRational &tb)
  {
    // since it could be inherited virtually, default-construct base classes then
    // set the parameters after its base classes are constructed
    type = mediatype;
    time_base = tb;

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
    std::unique_lock<std::mutex> l_tx(m);
    clear_threadunsafe();
  }

  int tryToPop(AVFrame *&frame)
  {
    std::unique_lock<std::mutex> l_tx(m);
    if (readyToPop_threadunsafe())
    {
      frame = pop_threadunsafe();
      return 0;
    }
    else
    {
      frame = NULL;
      return AVERROR(EAGAIN);
    }
  }

  void pop(AVFrame *&frame)
  {
    std::unique_lock<std::mutex> l_tx(m);
    while (!readyToPop_threadunsafe())
      cv_tx.wait(l_tx);
    frame = pop_threadunsafe();
  }

  template <class Predicate>
  int pop(AVFrame *&frame, Predicate pred)
  {
    std::unique_lock<std::mutex> l_tx(m);
    bool ready = true;
    while (ready && !readyToPop_threadunsafe())
      ready = cv_rx.wait(l_tx, pred);

    if (ready)
    {
      frame = pop_threadunsafe();
      return 0;
    }
    else
    {
      frame = NULL;
      return AVERROR(EAGAIN);
    }
  }

  int pop(AVFrame *&frame, const std::chrono::milliseconds &rel_time)
  {
    std::unique_lock<std::mutex> l_tx(m);
    std::cv_status status = std::cv_status::no_timeout;
    while (status == std::cv_status::no_timeout && !readyToPop_threadunsafe())
      status = cv_tx.wait_for(l_tx, rel_time);
    if (status == std::cv_status::no_timeout)
    {
      frame = pop_threadunsafe();
      return 0;
    }
    else
    {
      frame = NULL;
      return AVERROR(EAGAIN);
    }
  }

  template <class Predicate>
  int pop(AVFrame *&frame, const std::chrono::milliseconds &rel_time, Predicate pred)
  {
    std::unique_lock<std::mutex> l_tx(m);
    bool ready = true;
    while (ready && !readyToPop_threadunsafe())
      ready = cv_tx.wait_for(m, rel_time, pred);
    if (ready)
    {
      frame = pop_threadunsafe();
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
    std::unique_lock<std::mutex> l_rx(m);
    return readyToPop_threadunsafe();
  }

  void blockTillReadyToPop()
  {
    std::unique_lock<std::mutex> l_tx(m);
    while (!readyToPop_threadunsafe())
      cv_tx.wait(l_tx);
  }

  template <typename Pred>
  bool blockTillReadyToPop(Pred pred)
  {
    std::unique_lock<std::mutex> l_tx(m);
    bool ready = true;
    while (ready && !readyToPop_threadunsafe())
      ready = cv_rx.wait(l_tx, pred);
    return ready;
  }

  bool blockTillReadyToPop(const std::chrono::milliseconds &rel_time)
  {
    std::unique_lock<std::mutex> l_tx(m);
    std::cv_status status = std::cv_status::no_timeout;
    while (status == std::cv_status::no_timeout && !readyToPop_threadunsafe())
      status = cv_tx.wait_for(l_tx, rel_time);
    return status == std::cv_status::no_timeout;
  }

  template <typename Pred>
  void blockTillReadyToPop(const std::chrono::milliseconds &rel_time, Pred pred)
  {
    std::unique_lock<std::mutex> l_tx(m);
    bool ready = true;
    while (ready && !readyToPop_threadunsafe())
      ready = cv_rx.wait_for(l_tx, rel_time, pred);
    return ready;
  }

protected:
  virtual bool readyToPop_threadunsafe() const = 0;
  virtual AVFrame *pop_threadunsafe() = 0;
  virtual void clear_threadunsafe() = 0;

  std::condition_variable cv_tx;
};
}
