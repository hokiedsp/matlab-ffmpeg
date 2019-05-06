#include "ThreadBase.h"

#include <stdexcept>

ThreadBase::ThreadBase() : killnow(false), status(INIT) {}

ThreadBase::~ThreadBase()
{
  stop();
}
/////////////////////////////////////////////////////////////////////////////////////////////
// THREAD FUNCTIONS: Read file and send it to the FFmpeg decoder

bool ThreadBase::isRunning() const
{
  return thread.joinable();
}

void ThreadBase::start()
{
  // start the default file reading thread (sets up and idles)
  start(&ThreadBase::thread_fcn);
}

template <class Function, class... Args>
void ThreadBase::start(Function &&f, Args &&... args)
{
  // if there is an existing active thread, throw an exception
  if (thread.joinable())
    throw std::runtime_error("Cannot start a new thread. Only one thread per object.");

  killnow = false;

  // start the file reading thread (sets up and idles)
  thread = std::thread(f, this, args...);

  //start reading immediately
  resume();
}

void ThreadBase::pause()
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
    while (status != IDLE || status != PAUSED)
      thread_ready.wait(thread_guard);
  }
}

void ThreadBase::resume()
{
  std::unique_lock<std::mutex> thread_guard(thread_lock);
  status = ACTIVE;
  thread_ready.notify_one();
}

void ThreadBase::stop()
{
  // if thread has been detached or already terminated, nothing to do
  if (!thread.joinable())
    return;

  // pause the thread -> thread reaches PAUSED/IDLE state
  pause();

  // turn on the thread termination flag
  killnow = true;

  // resume the thread -> killnow must be checked right outside of IDLE wait
  resume();

  // wait till the thread has joined (if joinable)
  if (thread.joinable())
    thread.join();
}
