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
  static void initialize();                                                                // initialize the AVException static components
  static void force_throw();                                                               // throw exception with the previous log (only if it exists)
  static void log_error(const char *filename, int err, bool fatal = false);                // log error while operating on filename
  static void log(int log_level, const char *msg...); // log error while operating on filename

  static int av_log_level;   // set the minimum FFmpeg log level to throw exception
  static int av_throw_level; // set the minimum FFmpeg log level to throw exception

  static void (*log_fcn)(const std::string &line);

  static bool skip_repeated;

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
  virtual ~AVException() {}
};
