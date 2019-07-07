#pragma once

#include <chrono>
extern "C"
{
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
}

namespace ffmpeg
{

template <typename Chrono_t>
Chrono_t get_timestamp(uint64_t ts, const AVRational &tb)
{
  if constexpr (std::is_floating_point_v<typename Chrono_t::rep>)
  {
    std::chrono::duration<double> T(av_q2d(tb) * ts);
    return std::chrono::duration_cast<Chrono_t>(T);
  }
  else
  {
    return Chrono_t(
        av_rescale_q(ts, tb, {Chrono_t::period::num, Chrono_t::period::den}));
  }
}

} // namespace ffmpeg
