#pragma once

#include "ffmpegFilterBase.h"
#include "../ffmpegStream.h"
#include "../ffmpegMediaStructs.h"
#include "../ffmpegException.h"

extern "C" {
// #include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
// #include <libavfilter/avfiltergraph.h>
#include <libavutil/frame.h>
}

namespace ffmpeg
{
namespace filter
{
class EndpointBase : public Base, public MediaHandler, public AVFrameHandler
{
public:
  // AVMediaType type;   AVRational time_base
  EndpointBase(Graph &parent, const AVMediaType type, const AVRational &tb = {0, 0}) : Base(parent), MediaHandler(type, tb) {}
  EndpointBase(Graph &parent, const IMediaHandler &mdev) : Base(parent), MediaHandler(mdev) {}
  virtual ~EndpointBase()
  {
    av_log(NULL, AV_LOG_INFO, "destroyed EndpointBase\n");
  }

  virtual int processFrame() = 0;
};
}
}
