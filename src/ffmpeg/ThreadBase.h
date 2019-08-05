#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

class ThreadBase
{
  public:
  ThreadBase() : killnow(false), status(INIT) {}
  virtual ~ThreadBase() { stop(); }

  bool isRunning() const { return thread.joinable(); }
  bool isPaused() const { return status == PAUSED; }
  bool isInitializing(); // returns true if worker thread is still initializing
  void waitTillInitialized(); // blocks until the thread is out of
                              // initialization phase
  void start() { start(&ThreadBase::thread_fcn); }
  virtual void pause();
  virtual void resume();
  void stop();

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
  std::atomic<THREAD_STATUS> status; // see THREAD_STATUS definition above for

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
}

inline bool ThreadBase::isInitializing()
{
  // lock all mutexes
  std::unique_lock<std::mutex> thread_guard(thread_lock);
  return status == INIT;
}

inline void ThreadBase::waitTillInitialized()
{
  // blocks until the thread is out of initialization phase
  std::unique_lock<std::mutex> thread_guard(thread_lock);
  // cannot pause until thread has been initialized
  if (status == INIT)
    thread_ready.wait(thread_guard, [this]() { return status != INIT; });
}

inline void ThreadBase::pause()
{
  // lock all mutexes
  std::unique_lock<std::mutex> thread_guard(thread_lock);

  // cannot pause until thread has been initialized
  if (status == INIT)
    thread_ready.wait(thread_guard, [this]() { return status != INIT; });

  // if not already idling
  if (status != IDLE && status != PAUSED)
  {
    // command threads to pause
    status = PAUSE_RQ;
    thread_ready.notify_one();

    // wait until reader thread is IDLE
    thread_ready.wait(thread_guard,
                      [this]() { return status == IDLE || status == PAUSED; });
  }
}

inline void ThreadBase::resume()
{
  std::unique_lock<std::mutex> thread_guard(thread_lock);

  // thread is still initializing or idling (i.e., already running)
  if (status == INIT || status == IDLE || status ==ACTIVE) return;

  if (status != PAUSED)
    throw std::runtime_error("Cannot resume. Thread is not in paused state.");
  status = ACTIVE;
  thread_ready.notify_one();
}

inline void ThreadBase::stop()
{
  // if thread has been detached or already terminated, nothing to do
  if (!thread.joinable()) return;

  // pause the thread -> thread reaches PAUSED/IDLE state
  if (status != INIT) pause();

  // turn on the thread termination flag
  {
    std::unique_lock<std::mutex> thread_guard(thread_lock);
    killnow = true;
    thread_ready.notify_one();
  }

  // wait till the thread has joined (if joinable)
  if (thread.joinable()) thread.join();
}
