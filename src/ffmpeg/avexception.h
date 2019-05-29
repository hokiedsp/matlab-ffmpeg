#pragma once

#include <exception>
#include <string>
#include <shared_mutex>

extern "C"
{
#include <libavutil/log.h>
}

class AVException : public std::exception
{
public:
  const char *what() const noexcept
  {
    return errmsg.c_str();
  }

  // FFmpeg av_log
  static void initialize();                                                             // initialize the AVException static components
  static void log_error(int level, const char *msg) { log_error(nullptr, level, msg); } // fixed error log
  static void log_error(void *avcl, int level, const char *msg);                        // fixed error log

  // error log (if no further arg, err=err#; o.w., errpos)
  template <typename... Args>
  static void log_error(void *avcl, int level, const char *msgfmt, int err...)
  {
    if (sizeof...(Args))
    {
      int cnt = 0;
      static const auto swap = [err, &cnt](auto &&value) {
        if ((cnt++) == err)
        {
          char errmsg[AV_ERROR_MAX_STRING_SIZE];
          av_make_error_string(errmsg, AV_ERROR_MAX_STRING_SIZE, value);
          return errmsg;
        }
        else
          return value;
      };

      av_log(avcl, level, msgfmt, swap(std::forward<Args>(args))...);
    }
    else
    {
      char errmsg[AV_ERROR_MAX_STRING_SIZE];
      av_make_error_string(errmsg, AV_ERROR_MAX_STRING_SIZE, err);
      av_log(avcl, level, msgfmt, errmsg);
    }
    if (level <= av_throw_level)
      throw; // Should've already thrown AVException, but just in case
  }

  // error log (if no further arg, err=err#; o.w., errpos)
  template <typename... Args>
  static void log_error(int level, const char *msgfmt, int err, Args ...args)
  {
    AVException::log_error(nullptr, level, msgfmt, err, args...);
  }

  static void log(int log_level, const char *msg...) { log(nullptr, log_level, msg); } // general log
  static void log(void *avcl, int log_level, const char *msg...);                      // general log

  // error log (if no further arg, err=err#; o.w., errpos)
  template <typename... Args>
  static void sprintf_error(char *buff, size_t bufflen, const char *msgfmt, int err...)
  {
    if (sizeof...(Args))
    {
      int cnt = 0;
      static const auto swap = [err, &cnt](auto &&value) {
        if ((cnt++) == err)
        {
          char errmsg[AV_ERROR_MAX_STRING_SIZE];
          av_make_error_string(errmsg, AV_ERROR_MAX_STRING_SIZE, value);
          return errmsg;
        }
        else
          return value;
      };

      snprintf(buff, bufflen, msgfmt, swap(std::forward<Args>(args))...);
    }
    else
    {
      char errmsg[AV_ERROR_MAX_STRING_SIZE];
      av_make_error_string(errmsg, AV_ERROR_MAX_STRING_SIZE, err);
      snprintf(buff, bufflen, msgfmt, errmsg);
    }
  }

  static void throw_last_log();

  static int av_log_level;   // set the minimum FFmpeg log level to throw exception
  static int av_throw_level; // set the minimum FFmpeg log level to throw exception

  static void (*log_fcn)(const std::string &line); // callback for non-exception-throwing logs

  static bool skip_repeated;

  virtual ~AVException() {}
protected:
  /**
     * FFmpeg logging callback
     *
     * It prints the message to stderr, optionally colorizing it.
     *
     * @param avcl A pointer to an arbitrary struct of which the first field is a
     *        pointer to an AVClass struct.
     * @param level The importance level of the message expressed using a @ref
     *        lavu_log_constants "Logging Constant".
     * @param fmt The format string (printf-compatible) that specifies how
     *        subsequent arguments are converted to output.
     * @param vl The arguments referenced by the format string.
     */
  static void log_callback(void *ptr, int level, const char *fmt, va_list vl);

private:
  static std::shared_mutex mutex_;
  static bool initialized;
  static int print_prefix;
  static int count;
  static std::string prev;

  std::string errmsg;
  AVException(const char *line) : errmsg(line) {}
};
