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
  ffmpegException(const std::string &filename, int errnum) : std::runtime_error(print_error(filename, AVERROR(errnum))) {}
  ffmpegException(const std::string &errmsg) : std::runtime_error(errmsg) {}
  ~ffmpegException() {}

private:
  static std::string print_error(const std::string &filename, int err)
  {
    std::string errmsg;
    errmsg.reserve(AV_ERROR_MAX_STRING_SIZE+1);

    if (av_strerror(err, &errmsg.front(), errmsg.size())<0 || errmsg.empty())
    {
      errmsg = "Unknown error has occurred [AVERROR code = ";
      errmsg += std::to_string(err);
      errmsg +="].";
    }

    return errmsg;
  }
};
}
