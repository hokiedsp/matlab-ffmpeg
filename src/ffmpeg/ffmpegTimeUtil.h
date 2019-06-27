#pragma once

#include <chrono>
extern "C"
{
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
}

namespace ffmpeg
{
template <typename Chrono_t = std::chrono::nanoseconds>
Chrono_t get_timestamp(uint64_t ts, const AVRational &tb)
{
  return Chrono_t(
      av_rescale_q(ts, tb, {Chrono_t::period::num, Chrono_t::period::den}));
}
} // namespace ffmpeg
