#pragma once

#include "ffmpegFilterBase.h"
#include "../ffmpegStream.h"
#include "../ffmpegMediaStructs.h"

// extern "C" {
// // #include <libavcodec/avcodec.h>
// // #include <libavformat/avformat.h>
// #include <libavfilter/avfiltergraph.h>
// #include <libavutil/pixdesc.h>
// }

namespace ffmpeg
{
namespace filter
{
class EndpointBase : public Base, public MediaHandler
{
public:
  // AVMediaType type;   AVRational time_base
  explicit EndpointBase(Graph &parent, const AVMediaType type, const AVRational &tb = {0, 0}) : Base(parent), MediaHandler(type, tb) {}
  EndpointBase(Graph &parent, const IMediaHandler &mdev) : Base(parent), MediaHandler(mdev) {}
  virtual ~EndpointBase() {}

  virtual int processFrame() = 0;
};
}
}
