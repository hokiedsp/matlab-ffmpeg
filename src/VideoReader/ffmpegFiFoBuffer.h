#pragma once

#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <utility> // for std::pair
#include <functional>

#include <mex.h>

using namespace std::chrono_literals;

namespace ffmpeg
{

template <typename T>
class FifoBuffer
{
public:
  FifoBuffer(const uint32_t size = 0, const double timeout_s = 0.0, std::function<bool()> Predicate = std::function<bool()>())
      : max_wait_time_us(int64_t(timeout_s * 1e6) * 1us), enqueue_in_process(false),
        peek_count(0), pred(Predicate)
  {
    // this calls the base-class function. virtual does not kick in until the object is instantiated
    resize_internal(size);
  }

  void setTimeOut(const double timeout_s)
  {
    std::unique_lock<std::mutex> lck(mu);
    max_wait_time_us = int64_t(timeout_s * 1e6) * 1us;
  }

  void setPredicate(std::function<bool()> pred_fcn)
  {
    std::unique_lock<std::mutex> lck(mu);
    pred = pred_fcn;
  }

  // return the queue size
  uint32_t size() { return (uint32_t)len; }

  // release all waiting threads
  void releaseAll()
  {
    cv_rd.notify_all();
    cv_wr.notify_all();
  }

  // returns true if at least one thread is peeking at the head element
  bool peeked()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);
    bool rval = peek_count;
    lck.unlock();
    cv_rd.notify_one();
    cv_wr.notify_one();
    return rval;
  }

  // return the number of unread elements
  uint32_t elements()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);
    uint32_t rval = buffer.size();
    lck.unlock();
    cv_rd.notify_one();
    cv_wr.notify_one();
    return rval;
  }

  // return the number of unread elements
  uint32_t available()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);
    uint32_t rval = (uint32_t)(len - buffer.size());
    cv_rd.notify_one();
    cv_wr.notify_one();
    return rval;
  }

  void resize(const uint32_t size)
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    resize_internal(size);

    // notify writer if there is anyone waiting to write
    lck.unlock();
    cv_wr.notify_one();
  }

  void reset()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    reset_internal();

    // notify writer if there is anyone waiting to write
    lck.unlock();
    cv_wr.notify_one();
  }

  void enqueue(const T &elem) // copy construct
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    // block until reserved enqueue element has been released
    std::pair<bool, bool> wait_state = std::make_pair(false, pred());
    while (!wait_state.second && len == buffer.size())
    {
      wait_state = wait_to_work(lck, cv_wr);
      if (wait_state.first) // timed out
        throw ffmpegException("Timed out waiting to write: Buffer overflow.");
    }
    if (wait_state.second)
      return;

    enqueue_internal(elem);

    // done, unlock mutex and notify the condition variable
    lck.unlock();
    cv_rd.notify_one(); // there is something to read
  }

  T &enqueue_new() // emplace new element using its default constructor and returns it. Sets enqueue_in_progress flag. Must followup with enqueue_complete call to complete enqueueing
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    // block until reserved enqueue element has been released
    std::pair<bool, bool> wait_state = std::make_pair(false, pred());
    while (!wait_state.second && (len == buffer.size() || enqueue_in_process))
    {
      wait_state = wait_to_work(lck, cv_wr);
      if (wait_state.first) // timed out
        throw ffmpegException("Timed out waiting to write: Buffer overflow.");
    }

    buffer.emplace_back();
    T &rval = buffer.back();
    if (wait_state.second)
      return rval;

    enqueue_in_process = true;

    return rval;
  }

  template <typename... TArgs>
  T &enqueue_new(const bool lock, TArgs... args) // emplace new element (accepting variable arguments for its constructor) and returns it. Sets enqueue_in_progress flag if lock is true
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    // block until reserved enqueue element has been released
    std::pair<bool, bool> wait_state = std::make_pair(false, pred());
    while (!wait_state.second && (len == buffer.size() || enqueue_in_process))
    {
      wait_state = wait_to_work(lck, cv_wr);
      if (wait_state.first) // timed out
        throw ffmpegException("Timed out waiting to write: Buffer overflow.");
    }
    if (wait_state.second)
      return;

    T &rval = emplace_internal(args);

    // if further manipulation of enqueued element is necessary, set the in-process flag
    enqueue_in_process = lock;

    // done, unlock mutex and notify the condition variable
    lck.unlock();

    // notify waiting process only if queued item is not locked
    if (!enqueue_in_process)
      cv_rd.notify_one();

    return rval;
  }

  // Call this function to follow enqueue_new() or enqueue_new(true,...) to complete queueing process
  void enqueue_complete()
  {
    if (!enqueue_in_process)
      return;

    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    enqueue_in_process = false;

    lck.unlock();

    mexPrintf("Enqueue completed, notifying a reader\n");
    cv_rd.notify_one();
  }

  // Call this function to follow enqueue_new to cancel queueing process
  void enqueue_cancel()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    // relevant only if enqueueing proces was started previously by calling enqueue_new() function
    if (!enqueue_in_process)
      return;

    // let go of the reserved buffer element
    buffer.pop_back();
    enqueue_in_process = false;

    lck.unlock();
    cv_wr.notify_one(); // more room now
  }

  void dequeue(const bool was_peeking = false)
  {
    // if dequeue_after_peek is set, block until dequeue_after_peek is cleared
    // else if buffer is empty, block until an element is enqueued
    // otherwise, dequeue

    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    //
    if (was_peeking && peek_count)
      peek_count--;

    // if no new item has been written, wait
    std::pair<bool, bool> wait_state = std::make_pair(false, pred());
    while (!wait_state.second && (peek_count || buffer.empty() || (buffer.size() == 1 && enqueue_in_process)))
    {
      wait_state = wait_to_work(lck, cv_rd);
      if (wait_state.first) // timed out
      {
        if (peek_count)
          throw ffmpegException("Timed out waiting to read: A peeking worker is not releasing the element.");
        else if (buffer.empty())
          throw ffmpegException("Timed out waiting to read: Buffer underflow.");
      }
    }
    if (wait_state.second)
      return;

    // run the dequeue op, possibly overloaded
    dequeue_internal();

    // done, unlock mutex and notify the condition variable
    lck.unlock();
    cv_wr.notify_one();
  }

  // Access the next element without dequeueing. Use with caution as the element could be altered by
  // subsequent operation on the buffer.
  T &peek_next()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    // if no new item has been written, wait
    // block until an empty slot is available in the buffer
    // if no new item has been written, wait
    std::pair<bool, bool> wait_state = std::make_pair(false, pred());
    while (!wait_state.second && (buffer.empty() || (buffer.size() == 1 && enqueue_in_process)))
    {
      if (buffer.empty())
        mexPrintf("Nothing to peek...\n");
      else if (buffer.size() == 1 && enqueue_in_process)
        mexPrintf("Waiting for enqueue operation to be done...\n");

      wait_state = wait_to_work(lck, cv_rd);
      if (wait_state.first) // timed out
        throw ffmpegException("Timed out waiting to peek: Buffer underflow.");
    }

    // increment peeker count
    if (!wait_state.second)
      peek_count++;

    // get the head buffer content
    return peek_internal();
  }

  void peek_done()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    if (peek_count > 0)
      peek_count--; // if others are still peeking
    lck.unlock();
    if (!peek_count)
      cv.notify_all(); // let others know that nobody else is peeking
  }

protected:
  std::deque<T> buffer; // data storage

  virtual void resize_internal(const uint32_t size)
  {
    len = size;
    reset_internal();
  }

  virtual void reset_internal()
  {
    buffer.clear();
  }

  virtual void enqueue_internal(const T &elem) // copy construct
  {
    // place the new data onto the buffer
    buffer.push_back(elem);
  }

  template <typename... TArgs>
  T &emplace_internal(TArgs... args) // emplace with default constructor
  {
    return buffer.emplace_back(args...); // req. c++17
  }

  virtual void dequeue_internal()
  {
    // get the head buffer content
    buffer.pop_front();
  }

  virtual T &peek_internal()
  {
    return buffer.front();
  }

private:
  std::size_t len;         // buffer length
  bool enqueue_in_process; // true if enqueued element is still being worked on
  int peek_count;          // to peek the next item to be dequeued, keep counts to allow multiple peekers

  std::chrono::microseconds max_wait_time_us;
  std::mutex mu;                        // mutex to access the buffer
  std::condition_variable cv_rd, cv_wr; // condition variable to control the queue/dequeue flow
  std::function<bool()> pred;           // predicate

  // returns pair of bools. first bool: true if timed out; false if successfully released or released by predicate | second bool: predicate function output
  std::pair<bool, bool> wait_to_work(std::unique_lock<std::mutex> &lck, std::condition_variable &cv)
  {
    if (max_wait_time_us <= 0us) // no timeout
    {
      if (pred)
        cv.wait(lck, pred);
      else
        cv.wait(lck);
    }
    else if (pred)
    {
      while (!pred())
      {
        if (cv.wait_for(lck, max_wait_time_us) == std::cv_status::timeout)
          return std::make_pair(true, pred());
      }
    }
    else if (cv.wait_for(lck, max_wait_time_us) == std::cv_status::timeout)
      return std::make_pair(true, pred());

    return std::make_pair(false, true);
  }
};
}
