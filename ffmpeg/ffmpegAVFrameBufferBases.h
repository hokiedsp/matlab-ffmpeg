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
    const AVFrameSinkBase *other = dynamic_cast<const AVFrameSinkBase *>(&right);
    time_base = other.time_base;
  }

  // move assign operator
  virtual AVFrameSinkBase &operator=(AVFrameBufferBase &&right)
  {
    AVFrameSinkBase *other = dynamic_cast<const AVFrameSinkBase *>(&right);
    time_base = other.time_base;
  }

  bool readyToPush()
  {
    std::unique_lock<std::mutex> l_rx(m);
    return readyToPush_threadunsafe();
  }

  void blockTillReadyToPush()
  {
    std::unique_lock<std::mutex> l_rx(m);
    while (ready && !readyToPush_threadunsafe())
      cv_rx.wait(m);
  }
  bool blockTillReadyToPush(const std::chrono::milliseconds &rel_time)
  {
    std::unique_lock<std::mutex> l_rx(m);
    bool ready = true;
    while (ready && !readyToPush_threadunsafe())
      ready = cv_rx.wait_for(m, rel_time);
    return ready;
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
      cv_rx.wait(m);
    push_threadunsafe(frame);
  }

  template <class Predicate>
  int push(AVFrame *frame, Predicate pred)
  {
    std::unique_lock<std::mutex> l_rx(m);
    bool ready = true;
    while (ready && !readyToPush_threadunsafe())
      ready = cv_rx.wait(m, pred);
    if (!ready)
      return AVERROR(EAGAIN);

    push_threadunsafe(frame);
    return 0;
  }

  int push(AVFrame *frame, const std::chrono::milliseconds &rel_time)
  {
    std::unique_lock<std::mutex> l_rx(m);
    bool ready = true;
    while (ready && !readyToPush_threadunsafe())
      ready = cv_rx.wait_for(m, rel_time);
    if (!ready)
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
      cv_rx.wait(m);
    frame = pop_threadunsafe();
  }

  template <class Predicate>
  int pop(AVFrame *&frame, Predicate pred)
  {
    std::unique_lock<std::mutex> l_tx(m);
    bool ready = true;
    while (ready && !readyToPop_threadunsafe())
      ready = cv_rx.wait(m, pred);
    if (!ready)
      return AVERROR(EAGAIN);

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
    bool ready = true;
    while (ready && !readyToPop_threadunsafe())
      ready = cv_rx.wait_for(m, rel_time);
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

  template <class Predicate>
  int pop(AVFrame *&frame, const std::chrono::milliseconds &rel_time, Predicate pred)
  {
    std::unique_lock<std::mutex> l_tx(m);
    bool ready = true;
    while (ready && !readyToPop_threadunsafe())
      ready = cv_rx.wait_for(m, rel_time, pred);
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
    std::unique_lock<std::mutex> l_rx(m);
    while (!readyToPop_threadunsafe())
      cv_rx.wait(m); 
  }

  template<typename Pred>
  bool blockTillReadyToPop(Pred pred)
  {
    std::unique_lock<std::mutex> l_rx(m);
    bool ready = true;
    while (ready && !readyToPop_threadunsafe())
      ready = cv_rx.wait(m, pred);
    return ready;
  }
  
  void blockTillReadyToPop(const std::chrono::milliseconds &rel_time)
  {
    std::unique_lock<std::mutex> l_rx(m);
    bool ready = true;
    while (ready && !readyToPop_threadunsafe())
      ready = cv_rx.wait_for(m, rel_time);
    return ready;
  }

  template<typename Pred>
  void blockTillReadyToPop(const std::chrono::milliseconds &rel_time, Pred pred)
  {
    std::unique_lock<std::mutex> l_rx(m);
    bool ready = true;
    while  (ready && !readyToPop_threadunsafe())
      ready = cv_rx.wait_for(m, rel_time, pred);
    return ready;
  }

protected:
  virtual bool readyToPop_threadunsafe() const = 0;
  virtual AVFrame *pop_threadunsafe() = 0;

  std::condition_variable cv_tx;
};
}
