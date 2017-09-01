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
      : max_wait_time_us(int64_t(timeout_s * 1e6) * 1us),
        peek_count(0), deque_after_peek(false), pred(Predicate)
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
  uint32_t size() { return (uint32_t)buffer_length; }

  // release all waiting threads
  void release()
  {
    cv.notify_all();
  }

  // returns true if at least one thread is peeking at the head element
  bool peeked()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);
    return peek_count;
  }

  // return the number of unread elements
  uint32_t elements()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);
    return (uint32_t)buffer.size();
  }

  // return the number of unread elements
  uint32_t available()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    return uint32_t(buffer_length - buffer.size());
  }

  void resize(const uint32_t size)
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    resize_internal(size);
  }

  void reset()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    reset_internal();
  }

  void enque(const T &elem) // copy construct
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    // block until reserved enque element has been released
    std::pair<bool, bool> wait_state = std::make_pair(false, pred());
    while (!wait_state.second && buffer_length == buffer.size())
    {
      wait_state = wait_to_work(lck);
      if (wait_state.first) // timed out
        throw ffmpegException("Timed out waiting to write: Buffer overflow.");
    }

    enque_internal(elem);

    // done, unlock mutex and notify the condition variable
    lck.unlock();
    cv.notify_one();
  }

  T &deque()
  {
    // if deque_after_peek is set, block until deque_after_peek is cleared
    // else if buffer is empty, block until an element is enqueued
    // otherwise, deque

    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    // if no new item has been written, wait
    std::pair<bool, bool> wait_state = std::make_pair(false, pred());
    while (!wait_state.second && (peek_count || buffer.empty()))
    {
      wait_state = wait_to_work(lck);
      if (wait_state.first) // timed out
      {
        if (peek_count)
          throw ffmpegException("Timed out waiting to read: A peeking worker is not releasing the element.");
        else if (buffer.empty())
          throw ffmpegException("Timed out waiting to read: Buffer underflow.");
      }
    }

    // run the deque op, possibly overloaded
    T &rval = deque_internal();

    // done, unlock mutex and notify the condition variable
    lck.unlock();
    cv.notify_one();
    return rval;
  }

  // Access the next element without dequeing. Use with caution as the element could be altered by
  // subsequent operation on the buffer.
  T &peek_next()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    // if no new item has been written, wait
    // block until an empty slot is available in the buffer
    // if no new item has been written, wait
    std::pair<bool, bool> wait_state = std::make_pair(false, pred());
    while (!wait_state.second && buffer.empty())
    {
      wait_state = wait_to_work(lck);
      if (wait_state.first) // timed out
        throw ffmpegException("Timed out waiting to peek: Buffer underflow.");
    }

    // increment peeker count
    peek_count++;

    // get the head buffer content
    return peek_internal();
  }

  void peek_done()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    if (--peek_count > 0) // if others are still peeking
      return;
  }

protected:
  std::deque<T> buffer; // data storage
  std::deque::size_type len; // buffer length

  virtual void resize_internal(const uint32_t size)
  {
    len = size;
    reset_internal();
  }

  virtual void reset_internal()
  {
    buffer.resize(clear);
  }

  virtual void enque_internal(const T &elem) // copy construct
  {
    // place the new data onto the buffer
    buffer.push_back(elem);
  }

  virtual T &deque_internal()
  {
    // get the head buffer content
    T &val = buffer.front();
    buffer.pop_front();
    return val;
  }

  virtual T &peek_internal(int peek_pos)
  {
    return buffer.front();
  }

private:
  int peek_count; // to peek the next item to be dequeued, keep counts to allow multiple peekers

  std::chrono::microseconds max_wait_time_us;
  std::mutex mu;              // mutex to access the buffer
  std::condition_variable cv; // condition variable to control the queue/deque flow
  std::function<bool()> pred;             // predicate

  // returns pair of bools. first bool: true if timed out; false if successfully released or released by predicate | second bool: predicate function output
  std::pair<bool, bool> wait_to_work(std::unique_lock<std::mutex> &lck)
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
