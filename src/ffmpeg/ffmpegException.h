#pragma once

#include <stdexcept>

extern "C" {
#include <libavutil/avutil.h>
}

namespace ffmpeg
{
class ffmpegException : public std::runtime_error
{
public:
  ffmpegException(int ffmpegerrnum) : std::runtime_error(print_error("", AVERROR(ENOMEM))) {}
  ffmpegException(const std::string &filename, int errnum) : std::runtime_error(print_error(filename, errnum)) {}
  ffmpegException(const char *errmsg) : std::runtime_error(errmsg) {}
  ffmpegException(const std::string &errmsg) : std::runtime_error(errmsg) {}
  ~ffmpegException() {}

private:
  static std::string print_error(const std::string &filename, int err)
  {
    std::string errmsg;
    errmsg.reserve(128);
    if (av_strerror(err, (char*)errmsg.data(), errmsg.size()) < 0)
      errmsg = strerror(AVUNERROR(err));

    return errmsg;
  }
};
}
