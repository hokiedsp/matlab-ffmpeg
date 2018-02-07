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
class EndpointBase : public Base, protected BasicMediaParams, virtual public IMediaHandler
{
public:
  // AVMediaType type;   AVRational time_base
  EndpointBase(Graph &parent, const AVMediaType type, const AVRational &tb = {0, 0}) : Base(parent), BasicMediaParams({type, tb}) {}
  EndpointBase(Graph &parent, const IMediaHandler &mdev) : Base(parent), BasicMediaParams(mdev.getBasicMediaParams()) {}
  virtual ~EndpointBase() {}

  const BasicMediaParams &getBasicMediaParams() const { return (BasicMediaParams &)*this; }
  AVMediaType getMediaType() const { return type; }
  std::string getMediaTypeString() const { return av_get_media_type_string(type); }
  AVRational getTimeBase() const { return time_base; }
  void setTimeBase(const AVRational &tb) { time_base = tb; }

  virtual int processFrame() = 0;
};
}
}
