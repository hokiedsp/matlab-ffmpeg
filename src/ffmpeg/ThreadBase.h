#pragma once

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
// #include <atomic>

class ThreadBase
{
  public:
  ThreadBase() : killnow(false), status(INIT) {}
  virtual ~ThreadBase() { stop(); }

  virtual bool isRunning() const { return thread.joinable(); }
  virtual void start() { start(&ThreadBase::thread_fcn); }
  virtual void pause();
  virtual void resume();
  virtual void stop();

  protected:
  template <class Function, class... Args>
  void start(Function &&f, Args &&... args);

  std::thread thread; // read packets and send it to decoder
  virtual void thread_fcn() = 0;

// macro to rethrow any exception caught in the thread
#define assert_thread_exception()                                              \
  {                                                                            \
    if (eptr) std::rethrow_exception(eptr);                                    \
  }

  bool killnow; // true to kill member threads
  std::mutex thread_lock;
  std::condition_variable thread_ready;

  enum THREAD_STATUS
  {
    FAILED = -1, // set by thread_fcn() when it catches an exception
    INIT,        // initializing (default value, set by constructor)
    IDLE,     // idling (only set by thread_fcn() when it exhausts its work and
              // waiting for more)
    ACTIVE,   // working (only set by resume())
    PAUSE_RQ, // pause requested (only set by pause())
    PAUSED,   // paused (thread_fcn() may set status to this when it detects
              // PAUSE_RQ)
    REINIT_RQ // request to re-initialize (from derived class functions)
  };
  THREAD_STATUS status; // see THREAD_STATUS definition above for

  std::exception_ptr eptr;
};

/////////////////////////////////////////////////////////////////////////////////////////////
// THREAD FUNCTIONS: Read file and send it to the FFmpeg decoder

template <class Function, class... Args>
inline void ThreadBase::start(Function &&f, Args &&... args)
{
  // if there is an existing active thread, throw an exception
  if (thread.joinable())
    throw std::runtime_error(
        "Cannot start a new thread. Only one thread per object.");

  killnow = false;

  // start the file reading thread (sets up and idles)
  thread = std::thread(f, this, args...);

  // start reading immediately
  resume();
}

inline void ThreadBase::pause()
{
  // lock all mutexes
  std::unique_lock<std::mutex> thread_guard(thread_lock);

  // if not already idling
  if (status != IDLE || status != PAUSED)
  {
    // command threads to pause
    status = PAUSE_RQ;
    thread_ready.notify_one();

    // wait until reader thread is IDLE
    while (status != IDLE || status != PAUSED) thread_ready.wait(thread_guard);
  }
}

inline void ThreadBase::resume()
{
  std::unique_lock<std::mutex> thread_guard(thread_lock);
  if (status != ACTIVE || status != PAUSED)
    throw std::runtime_error("Cannot resume. Thread is not in paused state.");
  status = ACTIVE;
  thread_ready.notify_one();
}

inline void ThreadBase::stop()
{
  // if thread has been detached or already terminated, nothing to do
  if (!thread.joinable()) return;

  // pause the thread -> thread reaches PAUSED/IDLE state
  pause();

  // turn on the thread termination flag
  killnow = true;

  // resume the thread -> killnow must be checked right outside of IDLE wait
  resume();

  // wait till the thread has joined (if joinable)
  if (thread.joinable()) thread.join();
}
