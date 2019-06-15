#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

class NullMutex
{
};

template <typename MutexType>
class NullUniqueLock
{
  public:
  NullUniqueLock(MutexType m) noexcept {}
  NullUniqueLock(MutexType &m, std::defer_lock_t t) noexcept {}
  NullUniqueLock(MutexType &m, std::try_to_lock_t t) {}
  NullUniqueLock(MutexType &m, std::adopt_lock_t t) {}
  template <class Rep, class Period>
  NullUniqueLock(MutexType &m, const std::chrono::duration<Rep, Period> &timeout_duration) {}
  template <class Clock, class Duration>
  NullUniqueLock(MutexType &m, const std::chrono::time_point<Clock, Duration> &timeout_time) {}

  void lock() {}
  template <class Rep, class Period>
  bool try_lock_for(const std::chrono::duration<Rep, Period> &timeout_duration) { return true; }
  void unlock() {}
};

template <typename MutexLockType>
class NullConditionVariable
{
  public:
  void notify_one() {}

  void notify_all() {}

  template <typename MutexLockType>
  void wait(MutexLockType &lock) {}

  template <typename MutexLockType, class Predicate>
  void wait(MutexLockType &lock, Predicate pred)
  {
    while (!pred())
      ;
  }

  template <typename MutexLockType, class Rep, class Period>
  std::cv_status wait_for(MutexLockType &lock, const std::chrono::duration<Rep, Period> &rel_time) { return std::cv_status::no_timeout; }

  template <typename MutexLockType, class Rep, class Period, class Predicate>
  bool wait_for(MutexLockType &lock, const std::chrono::duration<Rep, Period> &rel_time, Predicate pred)
  {
    return wait_until(lock, std::chrono::high_resolution_clock::now() + rel_time, pred);
  }

  template <typename MutexLockType, class Clock, class Duration>
  std::cv_status wait_until(MutexLockType &lock, const std::chrono::time_point<Clock, Duration> &timeout_time) { return std::cv_status::no_timeout; }

  template <typename MutexLockType, class Clock, class Duration, class Pred>
  bool wait_until(MutexLockType &lock, const std::chrono::time_point<Clock, Duration> &timeout_time, Pred pred)
  {
    while (!pred() && std::chrono::high_resolution_clock::now() <= timeout_time)
      ;
    return pred();
  }
};

////////////////////////////////////////////////////////////////////
class Cpp11Mutex
{
  public:
  Cpp11Mutex() {}

  private:
  std::mutex m;

  template <typename MutexType, typename MutexImpl>
  friend class Cpp11UniqueLock;
};

template <typename MutexType, typename MutexImpl = std::mutex>
class Cpp11UniqueLock
{
  public:
  Cpp11UniqueLock(MutexType &m) noexcept : l(m.m) {}
  Cpp11UniqueLock(MutexType &m, std::defer_lock_t t) noexcept : l(m.m, t) {}
  Cpp11UniqueLock(MutexType &m, std::try_to_lock_t t) : l(m.m, t) {}
  Cpp11UniqueLock(MutexType &m, std::adopt_lock_t t) : l(m.m, t) {}
  template <class Rep, class Period>
  Cpp11UniqueLock(MutexType &m, const std::chrono::duration<Rep, Period> &timeout_duration) : l(m.m, timeout_duration) {}
  template <class Clock, class Duration>
  Cpp11UniqueLock(MutexType &m, const std::chrono::time_point<Clock, Duration> &timeout_time) : l(m.m, timeout_time) {}

  void lock() { l.lock(); }

  template <class Rep, class Period>
  bool try_lock_for(const std::chrono::duration<Rep, Period> &timeout_duration) { return l.try_lock_for(timeout_duration); }

  void unlock() { l.unlock(); }

  private:
  std::unique_lock<MutexImpl> l;

  friend class Cpp11ConditionVariable;
};

class Cpp11ConditionVariable
{
  public:
  Cpp11ConditionVariable() {}

  void notify_one() { cv.notify_one(); }

  void notify_all() { cv.notify_all(); }

  template <typename MutexLockType>
  void wait(MutexLockType &lock) { cv.wait(lock.l); }

  template <typename MutexLockType, class Predicate>
  void wait(MutexLockType &lock, Predicate pred) { cv.wait(lock.l, pred); }

  template <typename MutexLockType, class Rep, class Period>
  std::cv_status wait_for(MutexLockType &lock, const std::chrono::duration<Rep, Period> &rel_time) { return cv.wait_for(lock.l, rel_time); }

  template <typename MutexLockType, class Rep, class Period, class Predicate>
  bool wait_for(MutexLockType &lock, const std::chrono::duration<Rep, Period> &rel_time, Predicate pred) { return cv.wait_for(lock.l, rel_time, pred); }

  template <typename MutexLockType, class Clock, class Duration>
  std::cv_status wait_until(MutexLockType &lock, const std::chrono::time_point<Clock, Duration> &timeout_time) { return cv.wait_until(lock.l, timeout_time); }

  template <typename MutexLockType, class Clock, class Duration, class Pred>
  bool wait_until(MutexLockType &lock, const std::chrono::time_point<Clock, Duration> &timeout_time, Pred pred) { return cv.wait_until(lock.l, timeout_time, pred); }

  private:
  std::condition_variable cv;
};
