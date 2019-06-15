#pragma once

#include "ffmpegFilterEndpoints.h"

#include "../ffmpegException.h"
#include "../ffmpegStreamOutput.h"

// #include "mexClassHandler.h"
// #include "ffmpegPtrs.h"
// #include "ffmpegAvRedefine.h"
// #include "ffmpegAVFramePtrBuffer.h"
// #include "ffmpegFrameBuffers.h"

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

class SinkBase : public EndpointBase, public IAVFrameSource
{
  public:
  SinkBase(Graph &fg, IAVFrameSinkBuffer &buf); // connected to a buffer (data from non-FFmpeg source)
  virtual ~SinkBase();

  AVFilterContext *configure(const std::string &name = "");

  // Implementing IAVFrameSource interface
  IAVFrameSinkBuffer &getSinkBuffer() const
  {
    if (sink) return *sink;
    throw ffmpegException("No buffer.");
  }
  void setSinkBuffer(IAVFrameSinkBuffer &buf)
  {
    if (sink) sink->clrSrc();
    sink = &buf;
    sink->setSrc(*this);
  }
  void clrSinkBuffer()
  {
    if (sink)
    {
      sink->clrSrc();
      sink = NULL;
    }
  }
  // end Implementing IAVFrameSource interface

  /**
   * \brief Links the filter to another filter
   * 
   * Link this filter with another, overloads the base class function to force the last 2 arguments
   * 
   * \param other[inout]  Context of the other filter
   * \param otherpad[in]  The connector pad of the other filter
   * \param pad[in]  [ignore, default:0] The connector pad of this filter
   * \param issrc[in]  [ignore, default:false] Must be false (no output pad)
   * 
   * \throws ffmpegException if either \ref pad or \ref issrc arguments are incorrectly given.
   * \throws ffmpegException if either filter context is not ready.
   * \throws ffmpegException if filter contexts are not for the same filtergraph.
   * \throws ffmpegException if failed to link.
   */
  void link(AVFilterContext *other, const unsigned otherpad, const unsigned pad = 0, const bool issrc = false);

  /**
   * \brief Synchronize parameters to the internal AVFilterContext object
   * \returns true if sync success
   */
  virtual bool sync() = 0;

  /**
   * \brief Returns true if media parameters have been synced to the internal AVFilterContext object
   */
  bool isSynced() { return synced; }

  /**
   * \brief Check for existence of an output AVFrame from the filter graph and 
   *        if available output it to its sink buffer.
   * \returns True if new frame
   */
  virtual int processFrame();
  // virtual int processFrame(const std::chrono::milliseconds &rel_time);

  virtual void blockTillBufferReady() { sink->blockTillReadyToPush(); }
  virtual bool blockTillBufferReady(const std::chrono::milliseconds &rel_time) { return sink->blockTillReadyToPush(rel_time); }

  virtual bool enabled() const { return ena; };

  protected:
  IAVFrameSinkBuffer *sink;
  bool ena;

  bool synced;

  // make IMediaHandler interface read-only
  using IMediaHandler::setMediaParams;
  using IMediaHandler::setTimeBase;
};

typedef std::vector<SinkBase *> Sinks;

class VideoSink : public SinkBase, public VideoHandler
{
  public:
  VideoSink(Graph &fg, IAVFrameSinkBuffer &buf); // connected to a buffer (data from non-FFmpeg source)
  virtual ~VideoSink() {}

  AVFilterContext *configure(const std::string &name = "") override;

  /**
   * \brief Synchronize parameters to the internal AVFilterContext object
   * \returns true if sync success
   */
  bool sync() override;

  // std::string choose_pix_fmts();
  protected:
  using VideoHandler::setFormat;
  using VideoHandler::setWidth;
  using VideoHandler::setHeight;
  using VideoHandler::setSAR;
  };

class AudioSink : public SinkBase, public AudioHandler
{
  public:
  AudioSink(Graph &fg, IAVFrameSinkBuffer &buf); // connected to a buffer (data from non-FFmpeg source)
  virtual ~AudioSink() {}

  AVFilterContext *configure(const std::string &name = "") override;

  /**
   * \brief Synchronize parameters to the internal AVFilterContext object
   */
  bool sync() override;

  protected: // make AudioHandler read-only
  using AudioHandler::setFormat;
  using AudioHandler::setChannelLayout;
  using AudioHandler::setChannelLayoutByName;
  using AudioHandler::setSampleRate;
};
} // namespace filter
} // namespace ffmpeg