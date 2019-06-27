#pragma once

#include "ffmpegFilterEndpoints.h"

#include "../ffmpegAVFrameEndPointInterfaces.h"

extern "C"
{
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
class SourceBase : public EndpointBase, public IAVFrameSink
{
public:
  SourceBase(Graph &fg, IAVFrameSourceBuffer &srcbuf);
  virtual ~SourceBase();

  bool EndOfFile() { return eof; }

  // virtual AVFilterContext *configure(const std::string &name = "") = 0;
  // virtual void destroy(const bool deep = false);
  virtual void link(AVFilterContext *other, const unsigned otherpad, const unsigned pad = 0, const bool issrc = true);
  // void link(Base &other, const unsigned otherpad, const unsigned pad=0, const bool issrc=true);
  // virtual void parameters_from_stream()=0;
  // virtual void parameters_from_frame(const AVFrame *frame) = 0;

  virtual void blockTillFrameReady() { buf->blockTillReadyToPop(); }
  virtual bool blockTillFrameReady(const std::chrono::milliseconds &rel_time) { return buf->blockTillReadyToPop(rel_time); }
  virtual int processFrame();

  // Implementing IAVFrameSource interface
  IAVFrameSourceBuffer &getSourceBuffer() const
  {
    if (buf) return *buf;
    throw Exception("No buffer specified.");
  }
  void setSourceBuffer(IAVFrameSourceBuffer &new_buf)
  {
    if (buf)
      buf->clrDst();
    buf = &new_buf;
    buf->setDst(*this);
  }
  void clrSourceBuffer()
  {
    if (buf)
    {
      buf->clrDst();
      buf = NULL;
    }
  }
  // end implementing IAVFrameSource interface

  /**
   * \brief Load media parameters from its buffer
   */
  virtual bool updateMediaParameters() = 0;

protected:
  IAVFrameSourceBuffer *buf;
  bool eof;
  // AVBufferRef *hw_frames_ctx;
};

typedef std::vector<SourceBase *> Sources;

class VideoSource : public SourceBase, public VideoHandler
{
public:
  VideoSource(Graph &fg, IAVFrameSourceBuffer &srcbuf);
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

class AudioSource : public SourceBase, public AudioHandler
{
public:
  AudioSource(Graph &fg, IAVFrameSourceBuffer &srcbuf);
  virtual ~AudioSource() {}

  AVFilterContext *configure(const std::string &name = "");
  std::string generate_args();

  /**
   * \brief Load media parameters from its buffer
   */
  bool updateMediaParameters();

private:
};
} // namespace filter
} // namespace ffmpeg
