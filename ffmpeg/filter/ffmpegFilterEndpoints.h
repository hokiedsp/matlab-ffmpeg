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
class EndpointBase : public Base, protected BasicMediaParams, public IMediaHandler
{
public:
  EndpointBase(Graph &parent, const BasicMediaParams &params, BaseStream *s=NULL) : Base(parent), BasicMediaParams(params), st(s) {}
  virtual ~EndpointBase() {}

  const BasicMediaParams& getBasicMediaParams() const { return (BasicMediaParams&)*this; }
  AVMediaType getMediaType() const { return type; }
  std::string getMediaTypeString() const { return av_get_media_type_string(type); }
  AVRational getTimeBase() const { return time_base; }
  void setTimeBase(const AVRational &tb) { time_base = tb; }
  
  virtual int processFrame()=0;

protected:
  BaseStream *st;   // non-NULL if associated to an AVStream
};
}
}
