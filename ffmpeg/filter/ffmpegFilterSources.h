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
  virtual ~SourceBase();

  // virtual AVFilterContext *configure(const std::string &name = "") = 0;
  // virtual void destroy(const bool deep = false);
  virtual void link(AVFilterContext *other, const unsigned otherpad, const unsigned pad = 0, const bool issrc = true);
  // void link(Base &other, const unsigned otherpad, const unsigned pad=0, const bool issrc=true);
  // virtual void parameters_from_stream()=0;
  // virtual void parameters_from_frame(const AVFrame *frame) = 0;

  virtual void blockTillFrameReady() { src.blockTillReadyToPop(); }
  virtual bool blockTillFrameReady(const std::chrono::milliseconds &rel_time) { return src.blockTillReadyToPop(rel_time); }
  virtual int processFrame();

  /**
   * \brief Load media parameters from its buffer
   */
  virtual bool updateMediaParameters() = 0;

protected:
  IAVFrameSource &src;
  // AVBufferRef *hw_frames_ctx;
};

typedef std::vector<SourceBase *> Sources;

class VideoSource : public SourceBase, virtual public VideoHandler
{
public:
  VideoSource(Graph &fg, IAVFrameSource &buf);
  virtual ~VideoSource() {}

  AVFilterContext *configure(const std::string &name = "");
  std::string generate_args();

  /**
   * \brief Load media parameters from its buffer
   */
  bool updateMediaParameters();

private:
  int sws_flags;
};
class AudioSource : public SourceBase, virtual public AudioHandler
{
public:
  AudioSource(Graph &fg, IAVFrameSource &buf);
  virtual ~AudioSource() {}
  
  AVFilterContext *configure(const std::string &name = "");
  std::string generate_args();

  /**
   * \brief Load media parameters from its buffer
   */
  bool updateMediaParameters();

private:
};
}
}
