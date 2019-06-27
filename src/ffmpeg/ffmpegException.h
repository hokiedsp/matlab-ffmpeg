#pragma once

#include <cstdarg>
#include <exception>
#include <sstream>

extern "C"
{
#include <libavutil/avutil.h>
}

namespace ffmpeg
{
class Exception : public std::exception
{
  public:
  Exception(int ffmpegerrnum) { print_error("", AVERROR(ENOMEM)); }
  Exception(const std::string &filename, int errnum)
  {
    print_error(filename, AVERROR(errnum));
  }
  Exception(const std::string &errmsg) : message(errmsg) {}
  Exception(const char *format...)
  {
    char what_arg[1024];

    va_list argptr;
    va_start(argptr, format);
    vsnprintf(what_arg, 1024, format, argptr);
    va_end(argptr);

    message = what_arg;
  }
  virtual ~Exception() {}

  virtual const char *what() const throw() { return message.c_str(); }

  private:
  std::string message;

  void print_error(const std::string &filename, int err)
  {
    message.reserve(AV_ERROR_MAX_STRING_SIZE + 128);

    if (av_strerror(err, &message.front(), message.size()) < 0 ||
        message.empty())
    {
      message = "Unknown error has occurred [AVERROR code = ";
      message += std::to_string(err);
      message += "].";
    }
  }
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
};

/**
 * \brief To be thrown if selected stream/filter link is not of the expected
 *        type.
 */
class UnexpectedMediaType : public Exception
{
  public:
  UnexpectedMediaType(const AVMediaType expected, const AVMediaType given)
      : Exception(std::string("Unexpected media type: Expected ") +
                  av_get_media_type_string(expected) + " but received " +
                  av_get_media_type_string(given))
  {
  }
};
} // namespace ffmpeg
