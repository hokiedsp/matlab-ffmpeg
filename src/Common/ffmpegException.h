#pragma once

#include <cstdarg>
#include <exception>
#include <sstream>

extern "C" {
#include <libavutil/avutil.h>
}

namespace ffmpeg
{
class ffmpegException : public std::exception
{
public:
  ffmpegException(int ffmpegerrnum) { print_error("", AVERROR(ENOMEM)); }
  ffmpegException(const std::string &filename, int errnum) { print_error(filename, AVERROR(errnum)); }
  ffmpegException(const std::string &errmsg) : message(errmsg) {}
  ffmpegException(const char *format...)
  {
    char what_arg[1024];

    va_list argptr;
    va_start(argptr, format);
    vsnprintf(what_arg, 1024, format, argptr);
    va_end(argptr);

    message = what_arg;
  }
  ~ffmpegException() {}

  virtual const char *what() const throw() { return message.c_str(); }

private:
  std::string message;

  void print_error(const std::string &filename, int err)
  {
    message.reserve(AV_ERROR_MAX_STRING_SIZE + 128);

    if (av_strerror(err, &message.front(), message.size()) < 0 || message.empty())
    {
      message = "Unknown error has occurred [AVERROR code = ";
      message += std::to_string(err);
      message += "].";
    }
  }
};
}
