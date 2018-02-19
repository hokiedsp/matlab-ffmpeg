#pragma once

#include "ffmpegMediaStructs.h"

extern "C" {
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
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

inline void logPixelFormat(const AVPixFmtDescriptor *desc, const std::string &prefix = "")
{
  if (prefix.size())
    av_log(NULL, AV_LOG_INFO, "[%s] ", prefix.c_str());
  av_log(NULL, AV_LOG_INFO, "name:%s,|nb_components:%d|log2_chroma_w:%d|log2_chroma_h:%d\n",
         desc->name, desc->nb_components,desc->log2_chroma_w,desc->log2_chroma_h);
  for (int i = 0; i < desc->nb_components; ++i)
    av_log(NULL, AV_LOG_INFO, "\t[%d]plane:%d|step:%d|offset:%d|shift:%d|depth:%d\n",
           i, desc->comp[i].plane, desc->comp[i].step, desc->comp[i].offset, 
           desc->comp[i].shift, desc->comp[i].depth);
}
}
