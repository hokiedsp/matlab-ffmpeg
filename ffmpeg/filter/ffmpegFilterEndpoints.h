#pragma once

#include "ffmpegFilterBase.h"
#include "../ffmpegStream.h"

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

struct VideoParams
{
  int width, height;
  AVRational time_base;
  AVRational sample_aspect_ratio;
  AVPixelFormat format;
  // AVRational frame_rate;
};

class AudioParams
{
protected:
  AVSampleFormat format;
  AVRational time_base;
  int channels;
  uint64_t channel_layout;
  // int sample_rate;
};

class EndpointBase : public Base
{
public:
  EndpointBase(Graph &parent, AVMediaType mediatype) : Base(parent), st(NULL), type(mediatype) {}
  EndpointBase(Graph &parent, BaseStream &s, AVMediaType mediatype) : Base(parent), st(&s), type(mediatype) {}
  virtual ~EndpointBase() {}

  AVMediaType getMediaType() const { return type; }
  std::string getMediaTypeString() const { return av_get_media_type_string(type); }

  virtual int processFrame()=0;

protected:
  BaseStream *st;   // non-NULL if associated to an AVStream
  AVMediaType type; // AVMEDIA_TYPE_SUBTITLE for sub2video
};
}
}
