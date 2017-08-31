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
        rpos(-1), wpos(-1), buffer(0), pred(Predicate)
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
  uint32_t size() { return (uint32_t)buffer.size(); }

  // release all waiting threads
  void release()
  {
    cv.notify_all();
  }

  // returns true if a queue element has been reserved for the next write op
  bool reserved()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);
    return wpos_next >= 0;
  }

  // return the number of unread elements
  uint32_t elements()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    if (wpos < 0) // nothing written yet
      return 0;
    else if (wpos >= rpos) // last-written position is ahead of or the same as the last-read position
      return uint32_t(wpos - rpos);
    else // last-read position is ahead of the last-written position
      return uint32_t(wpos - rpos + size());
  }

  // return the number of unread elements
  uint32_t available()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    // last written position (treats a reserved element as written)
    int64_t wpos_last = (wpos_next < 0) ? wpos : wpos_next;

    if (wpos_last < 0) // nothing written yet
      return (uint32_t)size();
    else if (rpos <= wpos_last) // last-read position is ahead of last-written position
      return uint32_t(rpos - wpos_last + size());
    else
      return uint32_t(rpos - wpos_last);
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
    while (!wait_state.second && wpos_next >= 0)
    {
      wait_state = wait_to_work(lck);
      if (wait_state.first)
        throw ffmpegException("Timed out waiting to write: Other worker is not releasing the reserved slot.");
    }

    // increment the write position for the next write
    int64_t wpos_next_ = wpos + 1;

    // if write op caught up with read op, wait until space available
    if (((wpos>=0 && wpos == rpos) || (rpos == -1 && wpos_next_ == buffer.size())) && wait_to_work(lck).first)
      throw ffmpegException("Timed out waiting to write: Buffer overflow.");

    // finalize the next write position
    if (wpos_next_ == buffer.size())
      wpos = 0;
    else
      wpos = wpos_next_;

    enque_internal(elem);

    // done, unlock mutex and notify the condition variable
    lck.unlock();
    cv.notify_one();
  }

  T &reserve_next() // reserve a slot for the next item
  {
    mexPrintf("Buffer State:Pre-Reserve: rpos=%d; wpos=%d; wpos_next%d\n", rpos, wpos, wpos_next);
    mexPrintf("Buffer State:Pre-Reserve: %d:%d:%d\n", size(), elements(), available());

    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    // if another worker already reserved the next slot, must wait till the other is done
    std::pair<bool, bool> wait_state = std::make_pair(false, pred());
    while (wpos_next >= 0 && !wait_state.second)
    {
      wait_state = wait_to_work(lck);
      if (wait_state.first)
        throw ffmpegException("Timed out waiting to reserve an element ot enqueue: Another worker occupying reserved element.");
    }

    wpos_next = wpos + 1;
    if (((wpos>=0 && wpos == rpos) || (rpos == -1 && wpos_next == buffer.size())) && wait_to_work(lck).first)
      throw ffmpegException("Timed out waiting to reserve an element ot enqueue: Buffer overflow.");

      // reserve the next position
    if (wpos_next == buffer.size())
      wpos_next = 0;

    mexPrintf("Buffer State:Post-Reserve: rpos=%d; wpos=%d; wpos_next%d\n", rpos, wpos, wpos_next);

    lck.unlock();
    mexPrintf("Buffer State:Post-Reserve: %d:%d:%d\n", size(), elements(), available());

    // Let caller have the element in the next position
    return buffer[wpos_next];
  }

  void enque_reserved() // call to complete enquing the item in the reserved slot
  {
    mexPrintf("Buffer State:Pre-Commit: rpos=%d; wpos=%d; wpos_next%d\n", rpos, wpos, wpos_next);
    mexPrintf("Buffer State:Pre-Commit: %d:%d:%d\n", size(), elements(), available());

    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    if (wpos_next < 0)
      throw ffmpegException("Buffer not reserved.");

    // update the write position & clear enque-by-reference position
    wpos = wpos_next;
    wpos_next = -1;

    // done, unlock mutex and notify the condition variable
    lck.unlock();
    cv.notify_one();

    mexPrintf("Buffer State:Post-Commit: rpos=%d; wpos=%d; wpos_next%d\n", rpos, wpos, wpos_next);
    mexPrintf("Buffer State:Post-Commit: %d:%d:%d\n", size(), elements(), available());
  }

  void discard_reserved() //
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    if (wpos_next < 0)
      throw ffmpegException("Buffer not reserved.");

    // update the write position & clear enque-by-reference position
    wpos_next = -1;
  }

  T &deque()
  {
    // lock the mutex for the duration of this function
    std::unique_lock<std::mutex> lck(mu);

    // if no new item has been written, wait
    std::pair<bool, bool> wait_state = std::make_pair(false, pred());
    while (!wait_state.second && (wpos == rpos))
    {
      wait_state = wait_to_work(lck);
      if (wait_state.first)
        throw ffmpegException("Timed out waiting to read: Buffer underflow.");
    }

    // increment the write position
    if (++rpos == buffer.size())
      rpos = 0;

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
    while ((wpos == rpos) && !wait_state.second)
    {
      wait_state = wait_to_work(lck);
      if (wait_state.first)
        throw ffmpegException("Timed out waiting to read: Buffer underflow.");
    }

    // increment the write position
    int peek_pos = rpos + 1;
    if (peek_pos == buffer.size())
      peek_pos = 0;

    // get the head buffer content
    return peek_internal(peek_pos);
  }

protected:
  std::deque<T> buffer; // data storage
  bool write_in_progress; // if true, block any subsequent write op and any read op of the first in the queue

  virtual void resize_internal(const uint32_t size)
  {
    buffer.resize(size);
    rpos = wpos = wpos_next = -1;
  }

  virtual void reset_internal()
  {
    rpos = wpos = wpos_next = -1;
  }

  virtual void enque_internal(const T &elem) // copy construct
  {
    // place the new data onto the buffer
    buffer[wpos] = elem;
  }

  virtual T &deque_internal()
  {
    // get the head buffer content
    return buffer[rpos];
  }

  virtual T &peek_internal(int peek_pos)
  {
    return buffer[peek_pos];
  }

private:
  int64_t wpos_next; // position for enque-by-reference operation
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
