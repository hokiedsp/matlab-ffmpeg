#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace ffmpeg
{
template <typename T>
class FifoBuffer
{
 public:
   FifoBuffer(const int size, const double timeout_s = 0.0)
       : max_wait_time_us((timeout_s * 1e6) * std::chrono_literals::1us),
         rpos(-1), wpos(-1)
   {
      resize_internal(size);
   }

   void resize(const int size)
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

   void enque(const T &num) // copy construct
   {
      // lock the mutex for the duration of this function
      std::unique_lock<std::mutex> lck(mu);

      if (wpos_next >= 0)
      {
         if (max_wait_time_us <= 0) // no timeout
            cv.wait(lck);
         else if (!cv.wait_for(lck, max_wait_time_us))
            throw ffmpegException("Buffer overflow occurred");
      }

      // increment the write position
      if (++wpos == buffer.size())
         wpos = 0;

      // check for an available slot in the buffer; if not wait
      if (wpos == rpos)
      {
         if (max_wait_time_us <= 0) // no timeout
            cv.wait(lck);
         else if (!cv.wait_for(lck, max_wait_time_us))
            throw ffmpegException("Buffer overflow occurred");
      }

      // place the new data onto the buffer
      buffer[wpos] = num;

      // done, unlock mutex and notify the condition variable
      lck.unlock();
      cond.notify_one();
   }

   T &reserve_next() // reserve a slot for the next item
   {
      // lock the mutex for the duration of this function
      std::unique_lock<std::mutex> lck(mu);

      // reserve the next position
      wpos_next = wpos + 1;
      if (++wpos_next == buffer.size())
         wpos_next = 0;

      // check for an available slot in the buffer; if not wait
      if (wpos_next == rpos)
      {
         if (max_wait_time_us <= 0) // no timeout
            cv.wait(lck);
         else if (!cv.wait_for(lck, max_wait_time_us))
            throw ffmpegException("Buffer overflow occurred");
      }

      // place the new data onto the buffer
      return buffer[wpos_next];
   }

   void enqued_reserved() // call to complete enquing the item in the reserved slot
   {
      // lock the mutex for the duration of this function
      std::unique_lock<std::mutex> lck(mu);

      if (wpos_next < 0)
         throw ffmpegException("Buffer not reserved.");

      // update the write position & clear enque-by-reference position
      wpos = wpos_next;
      wpos_next = -1;

      // done, unlock mutex and notify the condition variable
      lck.unlock();
      cond.notify_one();
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
      if (wpos == rpos || wpos_next > 0)
      {
         if (max_wait_time_us <= 0) // no timeout
            cv.wait(lck, [this]() { return wpos != rpos; });
         else if (!cv.wait_for(lck, max_wait_time_us, [this]() { return wpos != rpos; }))
            throw ffmpegException("Buffer underflow occurred");
      }

      // increment the write position
      if (++rpos == buffer.size())
         rpos = 0;

      // get the head buffer content
      T &rval = buffer[rpos];

      // done, unlock mutex and notify the condition variable
      lck.unlock();
      cv.notify_one();
      return rval;
   }

 protected:
   std::vector<T> buffer; // data storage
   int rpos, wpos;        // last read/written position | -1 not set, cycles from 0 to buffer.size()-1

   virtual void resize_internal(const int size)
   {
      buffer.resize(size);
      wpos = rpos = wpos_next = -1;
   }

   virtual void reset_internal()
   {
      wpos = rpos = wpos_next = -1;
   }

 private:
   int wpos_next; // position for enque-by-reference operation
   std::chrono::microseconds max_wait_time_us;
   std::mutex mu;              // mutex to access the buffer
   std::condition_variable cv; // condition variable to control the queue/deque flow
};
}
