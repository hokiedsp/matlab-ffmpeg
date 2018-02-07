#pragma once

#include "ffmpegFilterEndpoints.h"

#include "../ffmpegAVFrameBufferBases.h"

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
  SourceBase(Graph &fg, IAVFrameSource &buf);

  // virtual AVFilterContext *configure(const std::string &name = "") = 0;
  // virtual void destroy(const bool deep = false);
  virtual void link(AVFilterContext *other, const unsigned otherpad, const unsigned pad = 0, const bool issrc = true);
  // void link(Base &other, const unsigned otherpad, const unsigned pad=0, const bool issrc=true);
  // virtual void parameters_from_stream()=0;
  // virtual void parameters_from_frame(const AVFrame *frame) = 0;

  virtual void blockTillFrameReady();
  virtual bool blockTillFrameReady(const std::chrono::milliseconds &rel_time);
  virtual int processFrame();

protected:
  IAVFrameSource *src;
  // AVBufferRef *hw_frames_ctx;
};

typedef std::vector<SourceBase *> Sources;

class VideoSource : public SourceBase, private VideoParams, virtual public IVideoHandler
{
public:
  VideoSource(Graph &fg, IAVFrameSource &buf);

  using SourceBase::getBasicMediaParams;
  const VideoParams& getVideoParams() const { return (VideoParams&)*this; }

  AVFilterContext *configure(const std::string &name = "");
  std::string generate_args();

  // void parameters_from_stream();
  // void parameters_from_frame(const AVFrame *frame);

private:
  int sws_flags;
};
class AudioSource : public SourceBase, private AudioParams, virtual public IAudioHandler
{
public:
  AudioSource(Graph &fg, IAVFrameSource &buf);
  
  using SourceBase::getBasicMediaParams;

  const AudioParams& getAudioParams() const { return (AudioParams&)*this; }

  AVFilterContext *configure(const std::string &name = "");
  std::string generate_args();

private:
};
}
}
