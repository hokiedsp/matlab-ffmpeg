#include <deque>
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
   FifoBuffer(const int size = 2, const double timeout_s = 0.0)
       : buffer(size), max_wait_time_us((timeout_s*1e6) * std::chrono_literals::1us),
         rpos(-1), wpos(-1)
   {
   }

   void enque(const T& num) // copy construct
   {
      // lock the mutex for the duration of this function
      std::unique_lock<std::mutex> lck(mu);

      // increment the write position
      if (++wpos==buffer.size()) wpos = 0;

      // check for an available slot in the buffer; if not wait
      if (wpos==rpos)
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

   T deque()
   {
      // lock the mutex for the duration of this function
      std::unique_lock<std::mutex> lck(mu);

      // increment the write position
      if (++rpos==buffer.size()) rpos = 0;

      // check for an available slot in the buffer; if not wait
      if (wpos==rpos)
      {
         if (max_wait_time_us <= 0) // no timeout
            cv.wait(lck, [this]() { return wpos!=rpos; });
         else if (!cv.wait_for(lck, max_wait_time_us, [this]() { return wpos!=rpos; }))
            throw ffmpegException("Buffer underflow occurred");
      }

      // pop the buffer content
      T rval = buffer[rpos];
      buffer.pop_front();

      // done, unlock mutex and notify the condition variable
      lck.unlock();
      cv.notify_one();
      return rval;
   }

 private:
   std::vector<T> buffer;  // data storage
   std::chrono::microseconds max_wait_time_us;
   int rpos, wpos; // -1 not set, cycles from 0 to buffer.size()-1
   std::mutex mu; // mutex to access the buffer
   std::condition_variable cv; // condition variable to control the queue/deque flow
};
}
