#pragma once

#include "ffmpegFilterEndpoints.h"

#include "../ffmpegStreamInput.h"
#include "../ffmpegAVFrameBufferInterfaces.h"

extern "C" {
// #include <libavfilter/avfiltergraph.h>
// #include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
// #include <libavutil/pixdesc.h>
}

#include <vector>

namespace ffmpeg
{
namespace filter
{

/**
 * 
 *
 */
class SourceBase : public EndpointBase
{
public:
  SourceBase(Graph &fg, AVMediaType mediatype);
  SourceBase(Graph &fg, InputStream &ist, AVMediaType mediatype);
  SourceBase(Graph &fg, IAVFrameSource &buf, AVMediaType mediatype);

  virtual void link(AVFilterContext *other, const unsigned otherpad, const unsigned pad = 0, const bool issrc = true);
  virtual void parameters_from_stream()=0;
  virtual void parameters_from_frame(const AVFrame *frame) = 0;

protected:
  IAVFrameSource *src;
  AVBufferRef *hw_frames_ctx;
};

typedef std::vector<SourceBase *> Sources;

class VideoSource : public SourceBase, private VideoParams
{
public:
  VideoSource(Graph &fg);
  VideoSource(Graph &fg, InputStream &ist); // connected to an FFmpeg stream
  VideoSource(Graph &fg, IAVFrameSource &buf, VideoParams &params);   // connected to a buffer (data from non-FFmpeg source)
  AVFilterContext *configure(const std::string &name = "");
  std::string generate_args();

  void parameters_from_stream();
  void parameters_from_frame(const AVFrame *frame);

private:
  int sws_flags;
};
class AudioSource : public SourceBase, private AudioParams
{
public:
  AudioSource(Graph &fg);
  AudioSource(Graph &fg, InputStream &ist);
  AudioSource(Graph &fg, IAVFrameSource &buf, AudioParams &params); // connected to a buffer (data from non-FFmpeg source)
  AVFilterContext *configure(const std::string &name = "");
  std::string generate_args();

  void parameters_from_stream();
  void parameters_from_frame(const AVFrame *frame);

private:
  void get_stream_parameters();
};
}
}
