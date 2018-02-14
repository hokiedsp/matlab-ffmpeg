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
class EndpointBase : public Base, public MediaHandler
{
public:
  // AVMediaType type;   AVRational time_base
  EndpointBase(Graph &parent, const AVMediaType type, const AVRational &tb = {0, 0}) : Base(parent), MediaHandler(type, tb)
  {
    frame = av_frame_alloc();
    if (!frame)
      throw ffmpegException("[ffmpeg::filter::SourceBase]Failed to allocate AVFrame.");
  }
  EndpointBase(Graph &parent, const IMediaHandler &mdev) : Base(parent), MediaHandler(mdev)
  {
    frame = av_frame_alloc();
    if (!frame)
      throw ffmpegException("[ffmpeg::filter::SourceBase]Failed to allocate AVFrame.");
  }
  virtual ~EndpointBase()
  {
    av_log(NULL, AV_LOG_INFO, "destroying EndpointBase\n");
    if (frame) av_frame_free(&frame);
    av_log(NULL, AV_LOG_INFO, "destroyed EndpointBase\n");
  }

  virtual int processFrame() = 0;

protected:
  AVFrame *frame; // allocated during construction, unref after at the end of processFrame, free during destruction
};
}
}
