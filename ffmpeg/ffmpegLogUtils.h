#pragma once

#include "ffmpegMediaStructs.h"

extern "C" {
#include <libavutil/log.h>
}

#include <string>

namespace ffmpeg
{

inline std::string getRationalString(AVRational q)
{
  return std::to_string(q.num) + '/' + std::to_string(q.den);
}

inline void logVideoParams(const VideoParams &params, const std::string &fcn_name = "???")
{
  av_log(NULL, AV_LOG_INFO, "[%s] Video Parameters::format:%s::width:%d::height:%d::sar:%s\n",
         fcn_name.c_str(), (params.format != AV_PIX_FMT_NONE) ? av_get_pix_fmt_name(params.format) : "none",
         params.width, params.height, getRationalString(params.sample_aspect_ratio).c_str());
}
}
