#pragma once

#include <vector>
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
struct FifoContainer
{
  T data;
  enum
  {
    EMPTY,
    BEING_WRITTEN,
    WRITTEN,
    BEING_READ,
    READ
  } status;

  FifoContainer() : status(EMPTY){};
  FifoContainer(const T &src) : FifoContainer(), data(src){};
  bool is_writable(unsigned min_read = 1) { return (status == EMPTY) || (status == READ); }
  bool is_readable() { return status == WRITTEN; }
  bool is_busy() { return status == BEING_WRITTEN || status == BEING_READ; }

  virtual void init()
  {
    status = EMPTY;
  }

  virtual T *write_init()
  {
    if (status == BEING_READ)
      throw ffmpegException("Data is being read.");
    status = BEING_WRITTEN;
    return &data;
  }

  virtual bool write_done(const T *ref)
  {
    bool matched = (&data == ref && status == BEING_WRITTEN);
    if (matched)
      status = WRITTEN;
    return matched;
  }

  virtual bool write_cancel(const T *ref)
  {
    bool matched = (&data == ref && status == BEING_WRITTEN);
    if (matched)
      status = EMPTY;
    return matched;
  }

  virtual T *read_init()
  {
    if (status != WRITTEN)
      throw ffmpegException("No data to read.");
    status = BEING_READ;
    return &data;
  }
  virtual bool read_done(const T *ref)
  {
    bool matched = (&data == ref && status == BEING_READ);
    if (matched)
      status = READ;
    return matched;
  }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename T, typename C = FifoContainer<T>>
class FifoBuffer
{
  typedef std::vector<C> FifoVector_t;

public:
  FifoBuffer(const unsigned nelem = 0, const double timeout_s = 0.0, std::function<bool()> Predicate = std::function<bool()>())
      : pred(Predicate)
  {
    resize(nelem);
    rptr = wptr = buffer.begin();
  }

  void setPredicate(std::function<bool()> pred_fcn)
  {
    std::unique_lock<std::mutex> guard(lock);
    pred = pred_fcn;
  }

  void releaseAll()
  {
    cond_recv.notify_all();
    cond_send.notify_all();
  }

  // return the queue size
  uint32_t size() const { return buffer.size(); }
  bool empty() const { return buffer.empty(); }
  uint32_t elements()
  {
    std::unique_lock<std::mutex> guard(lock);

    if (!rptr->is_readable())
      return 0;

    uint32_t cnt = 1;
    FifoVector_t::iterator it = rptr + 1;
    for (; it->is_readable() && it < buffer.end(); it++)
      cnt++;
    if (it != buffer.end())
      return cnt;
    for (it = buffer.begin(); it->is_readable() && it < rptr; it++)
      cnt++;
    return cnt;
  }
  uint32_t available()
  {
    std::unique_lock<std::mutex> guard(lock);

    if (!wptr->is_writable())
      return 0;

    uint32_t cnt = 1;
    FifoVector_t::iterator it = wptr + 1;
    for (; it->is_writable() && it < buffer.end(); it++)
      cnt++;
    if (it != buffer.end())
      return cnt;
    for (it = buffer.begin(); it->is_writable() && it < wptr; it++)
      cnt++;
    return cnt;
  }

  void resize(const uint32_t size)
  {
    std::unique_lock<std::mutex> guard(lock);

    // clear the content
    flush_locked(true);

    // adjust the size of pool
    buffer.resize(size);
    guard.unlock();
    cond_send.notify_one();
  }

  // return the next container to be filled and mark it being_filled
  T *get_container(const double timeout_s = 0.0f)
  {
    std::unique_lock<std::mutex> guard(lock);

    bool killnow;
    while (!((killnow = pred()) || wptr->is_writable()))
      wait(guard, cond_send, timeout_s);

    if (killnow) return NULL;
    return wptr->write_init();
  }

  // mark the next container as filled
  void send(const T *ref)
  {
    std::unique_lock<std::mutex> guard(lock);
    if (!wptr->write_done(ref))
      throw ffmpegException("Trying to send a container which was not passed by the last get_container() call.");

    // advance the write pointer
    if (++wptr == buffer.end())
      wptr = buffer.begin();

    guard.unlock();
    cond_recv.notify_one();
  }

  // mark the next container as empty
  void send_cancel(const T *ref)
  {
    std::unique_lock<std::mutex> guard(lock);
    if (!wptr->write_cancel(ref))
      throw ffmpegException("Trying to cancel sending a container which was not passed by the last get_container() call.");
    guard.unlock();
    cond_send.notify_one();
  }

  // peek the next filled container
  T *recv(const double timeout_s = 0.0f)
  {
    std::unique_lock<std::mutex> guard(lock);

    bool killnow;
    while (!((killnow = pred()) || rptr->is_readable()))
      wait(guard, cond_recv, timeout_s);

    if (killnow) return NULL;
    return rptr->read_init();
  }

  // done peeking the container
  void recv_done(const T *ref)
  {
    std::unique_lock<std::mutex> guard(lock);

    if (!rptr->read_done(ref)) // makes container status to READ (now can be written)
      throw ffmpegException("Given container is not the one returned by the last recv_init() call.");

    // advance the read pointer
    if (++rptr == buffer.end())
      rptr = buffer.begin();

    // bool matched = false;
    // FifoVector_t::iterator it = rptr;
    // do
    // {
    //   matched = it->read_done(ref);
    // } while (!matched || it != buffer.begin());
    // for (it = buffer.end() - 1; !matched || it != rptr; --it)
    //   matched = it->read_done(ref);

    // if (!matched)
    //   throw ffmpegException("ffmpegFifoBuffer could not find matching data.");

    guard.unlock();
    // if (!it->is_writable())
    cond_send.notify_one();
  }

  //
  bool flush(bool force = false)
  {
    std::unique_lock<std::mutex> guard(lock);

    if (!flush_locked(force))
      return false;

    guard.unlock();
    cond_send.notify_one();

    return true;
  }

private:
  FifoVector_t buffer;
  typename FifoVector_t::iterator rptr;
  typename FifoVector_t::iterator wptr;

  std::mutex lock;                              // mutex to access the buffer
  std::condition_variable cond_recv, cond_send; // condition variable to control the queue/dequeue flow
  std::function<bool()> pred;                   // predicate

  void wait(std::unique_lock<std::mutex> &lock, std::condition_variable &cond, const double duration)
  {
    if (duration > 0.0f)
      cond.wait_for(lock, int64_t(duration * 1e6) * 1us);
    else
      cond.wait(lock);
  }

  bool flush_locked(bool force)
  {
    // if anything still writing/reading
    if (!force)
      for (FifoVector_t::iterator it = buffer.begin(); it < buffer.end(); it++)
        if (it->is_busy())
          return false;

    for (FifoVector_t::iterator it = buffer.begin(); it < buffer.end(); it++)
      it->init();

    wptr = rptr = buffer.begin();

    return true;
  }
};
}
