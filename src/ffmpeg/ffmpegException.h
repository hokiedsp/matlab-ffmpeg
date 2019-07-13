#pragma once

#include <exception>
#include <regex>
#include <shared_mutex>
#include <string>

extern "C"
{
#include <libavutil/log.h>
}

namespace ffmpeg
{
class Exception : public std::exception
{
  public:
  Exception(const std::string line) : errmsg(line) {}

  template <typename... Args, size_t MAX_ERR_LEN = 128>
  Exception(const char *fmt, Args... args)
  {
    char msg[MAX_ERR_LEN];
    snprintf(msg, MAX_ERR_LEN, fmt, args...);
    errmsg = msg;
  }

  Exception(const int err) : errmsg()
  {
    char msg[AV_ERROR_MAX_STRING_SIZE];
    av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, err);
    errmsg = msg;
  }

  // use "[errmsg]" as a placeholder in fmt to insert ffmpeg error string
  template <typename... Args, size_t MAX_ERR_LEN = 128>
  Exception(int errno, const char *fmt, Args... args)
  {
    char msg[MAX_ERR_LEN];
    snprintf(msg, MAX_ERR_LEN, fmt, args...);
    errmsg = msg;

    char errtxt[AV_ERROR_MAX_STRING_SIZE];
    av_make_error_string(errtxt, AV_ERROR_MAX_STRING_SIZE, errno);

    std::regex re("\\[errmsg\\]");
    errmsg = std::regex_replace(errmsg, re, errtxt);
  }

  virtual ~Exception() {}

  const char *what() const noexcept { return errmsg.c_str(); }

  // FFmpeg av_log
  static void initialize(); // initialize the AVException static components

  template <typename... Args>
  static void log(int log_level, const char *msg, Args... args)
  {
    av_log(nullptr, log_level, msg, args...);
  }

  static int
      av_log_level; // set the minimum FFmpeg log level to throw exception
  static int
      av_throw_level; // set the minimum FFmpeg log level to throw exception

  static void (*log_fcn)(
      const std::string &line); // callback for non-exception-throwing logs

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
};

/**
 * \brief To be thrown if stream/filter link specifier is not valid
 */
class InvalidStreamSpecifier : public Exception
{
  public:
  InvalidStreamSpecifier(const std::string &spec)
      : Exception(std::string("Invalid stream specifier: ") + spec)
  {
  }
  InvalidStreamSpecifier(const int id)
      : Exception(std::string("Invalid stream ID: ") + std::to_string(id))
  {
  }
  InvalidStreamSpecifier(const AVMediaType type)
      : Exception(std::string("Invalid stream type: ") +
                  std::string(av_get_media_type_string(type)))
  {
  }
};

/**
 * \brief To be thrown if selected stream/filter link is not of the expected
 *        type.
 */
class UnexpectedMediaType : public Exception
{
  public:
  UnexpectedMediaType(const AVMediaType expected, const AVMediaType given)
      : Exception("Unexpected media type: Expected %s but received %s.",
                  av_get_media_type_string(expected),
                  av_get_media_type_string(given))
  {
  }
};
} // namespace ffmpeg
