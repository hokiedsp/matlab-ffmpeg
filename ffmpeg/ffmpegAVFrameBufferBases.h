#pragma once

#include "AVFrameBufferInterfaces.h"

#include <mutex>
#include <condition_variable>

namespace ffmpeg
{
// Base Thread-Safe Buffer Class
class AVFrameBufferBase
{
public:
  virtual ~AVFrameBufferBase() {}

  // copy assign operator
  virtual AVFrameBufferBase &operator=(const AVFrameBufferBase &other) {}
  // move assign operator
  virtual AVFrameBufferBase &operator=(AVFrameBufferBase &&other) {}

protected:
  std::mutex m; // mutex to access the buffer
}

// Base Thread-Safe Sink Class
class AVFrameSinkBase : public IAVFrameSink,
                        virtual private AVFrameBufferBase
{
public:
  virtual ~AVFrameSinkBase(){};
  AVFrameSinkBase() : time_base{0, 0} {}
  AVFrameSinkBase(const AVRational &tb) : time_base(tb) {}

  // copy constructor
  AVFrameSinkBase(const AVFrameSinkBase &other) : time_base(other.time_base) {}

  // move constructor
  AVFrameSinkBase(AVFrameSinkBase &&other) noexcept : time_base(other.time_base) {}

  // copy assign operator
  virtual AVFrameSinkBase &operator=(const AVFrameBufferBase &other)
  {
    const AVFrameSinkBase *other = dynamic_cast<const AVFrameSinkBase*>(&right);
    time_base = other.time_base;
  }

  // move assign operator
  virtual AVFrameSinkBase &operator=(AVFrameBufferBase &&right)
  {
    AVFrameSinkBase *other = dynamic_cast<const AVFrameSinkBase*>(&right);
    time_base = other.time_base;
  }
  
  bool readyToPush()
  {
    std::unique_lock<std::mutex> l_rx(m);
    return readyToPush_threadunsafe();
  }
  
  int push(AVFrame *frame)
  {
    std::unique_lock<std::mutex> l_rx(m);
    if (!readyToPush())
      cv_rx.wait(m);
    push_threadunsafe(frame);
  }
  template <class Predicate>
  int push(AVFrame *frame, Predicate pred)
  {
    std::unique_lock<std::mutex> l_rx(m);
    if (!readyToPush())
      cv_rx.wait(m, pred);
    push_threadunsafe(frame);
  }

  AVRational getTimeBase() const { return time_base; }
  virtual void setTimeBase(const AVRational &tb) { time_base = tb; }

protected:
  virtual bool readyToPush_threadunsafe() const = 0;
  virtual int push_threadunsafe(AVFrame *frame) = 0;

  AVRational time_base;
  std::condition_variable cv_rx;
};

// Base Source Class
class AVFrameSourceBase : public IAVFrameSource, virtual AVFrameBufferBase
{
public:
  virtual ~AVFrameSourceBase(){};
  // copy constructor
  AVFrameSourceBase(const AVFrameSinkBase &other) {}
  // move constructor
  AVFrameSourceBase(AVFrameSinkBase &&other) noexcept {}
  // copy assign operator
  virtual AVFrameSourceBase &operator=(const AVFrameBufferBase &other) {}
  // move assign operator
  virtual AVFrameSourceBase &operator=(AVFrameBufferBase &&other) {}

  AVFrame *pop()
  {
    std::unique_lock<std::mutex> l_tx(m);
    if (!readyToPop())
      cv_rx.wait(m);
    return pop_threadunsafe();
  }

  template <class Predicate>
  AVFrame *pop(Predicate pred)
  {
    std::unique_lock<std::mutex> l_tx(m);
    if (!readyToPop())
      cv_rx.wait(m, pred);
    return pop_threadunsafe();
  }

  bool readyToPop()
  {
    std::unique_lock<std::mutex> l_rx(m);
    return readyToPop_threadunsafe();
  }

protected:
  virtual bool readyToPop_threadunsafe() const = 0;
  virtual AVFrame *pop_threadunsafe() = 0;

  std::condition_variable cv_tx;
};
}
