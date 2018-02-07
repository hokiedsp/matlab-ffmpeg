#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
// #include <atomic>

class ThreadBase
{
public:
  ThreadBase();
  virtual ~ThreadBase();

  virtual bool isRunning() const;
  virtual void start(); // starts a new thread (stops existing thread)
  virtual void pause();
  virtual void resume();
  virtual void stop();

protected:
  template <class Function, class... Args>
  void start(Function &&f, Args &&... args);

  std::thread thread; // read packets and send it to decoder
  virtual void thread_fcn() = 0;

// macro to rethrow any exception caught in the thread
#define assert_thread_exception()   \
  {                                 \
    if (eptr)                       \
      std::rethrow_exception(eptr); \
  }

  bool killnow; // true to kill member threads
  std::mutex thread_lock;
  std::condition_variable thread_ready;

  enum THREAD_STATUS
  {
    FAILED = -1, // set by thread_fcn() when it catches an exception
    INIT,        // initializing (default value, set by constructor)
    IDLE,        // idling (only set by thread_fcn() when it exhausts its work and waiting for more)
    ACTIVE,      // working (only set by resume())
    PAUSE_RQ,    // pause requested (only set by pause())
    PAUSED       // paused (thread_fcn() may set status to this when it detects PAUSE_RQ)
  };
  THREAD_STATUS status; // see THREAD_STATUS definition above for 

  std::exception_ptr eptr;
};
