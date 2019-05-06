#pragma once

#include "ffmpegFilterEndpoints.h"

#include "../ffmpegStreamOutput.h"
#include "../ffmpegException.h"

// #include "mexClassHandler.h"
// #include "ffmpegPtrs.h"
// #include "ffmpegAvRedefine.h"
// #include "ffmpegAVFramePtrBuffer.h"
// #include "ffmpegFrameBuffers.h"

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

class SinkBase : public EndpointBase
{
public:
  SinkBase(Graph &fg, IAVFrameSink &buf); // connected to a buffer (data from non-FFmpeg source)
  virtual ~SinkBase();

  AVFilterContext *configure(const std::string &name = "");

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
   */
  virtual void sync() = 0;

  /**
   * \brief Check for existence of an output AVFrame from the filter graph and 
   *        if available output it to its sink buffer.
   * \returns True if new frame
   */
  virtual int processFrame();
  virtual int processFrame(const std::chrono::milliseconds &rel_time);

  virtual void blockTillBufferReady() { sink.blockTillReadyToPush(); }
  virtual bool blockTillBufferReady(const std::chrono::milliseconds &rel_time) { return sink.blockTillReadyToPush(rel_time); }

  virtual bool enabled() const { return ena; };

protected:
  IAVFrameSink &sink;
  bool ena;
};

typedef std::vector<SinkBase *> Sinks;

class VideoSink : public SinkBase, public VideoHandler
{
public:
  VideoSink(Graph &fg, IAVFrameSink &buf);      // connected to a buffer (data from non-FFmpeg source)
  virtual ~VideoSink(){}

  AVFilterContext *configure(const std::string &name = "");
  /**
   * \brief Synchronize parameters to the internal AVFilterContext object
   */
  void sync();

  // std::string choose_pix_fmts();
};

class AudioSink : public SinkBase, public AudioHandler
{
public:
  AudioSink(Graph &fg, IAVFrameSink &buf); // connected to a buffer (data from non-FFmpeg source)
  virtual ~AudioSink(){}

  AVFilterContext *configure(const std::string &name = "");

  /**
   * \brief Synchronize parameters to the internal AVFilterContext object
   */
  void sync();
};
}
}