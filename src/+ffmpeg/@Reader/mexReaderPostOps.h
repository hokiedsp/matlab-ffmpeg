#pragma once

// #include <chrono>
#include <sstream>
#include <string>
// #include <unordered_map>
// #include <vector>

extern "C"
{
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
}

#include <ffmpegAVFrameQueue.h>
#include <syncpolicies.h>

#include <ffmpegPostOp.h>
#include <filter/ffmpegFilterGraph.h>

/**
 * \brief a FFmpeg video filter to convert a video AVFrame to desired format &
 * orientation
 */
class mexFFmpegVideoPostOp : public ffmpeg::PostOpInterface
{
  public:
  mexFFmpegVideoPostOp(ffmpeg::IAVFrameSourceBuffer &src,
                       const AVPixelFormat pixfmt)
      : out(1)
  {
    // create filter graph
    std::ostringstream ssout;
    ssout << "[in]transpose,format=pix_fmts=" << av_get_pix_fmt_name(pixfmt)
          << "[out]";
    fg = ffmpeg::filter::Graph(ssout.str());

    // Link filter graph to the in/out buffers
    fg.assignSource(src, "in");
    fg.assignSink(out, "out");

    // finalize the filter configuration
    fg.configure();
  }
  ~mexFFmpegVideoPostOp() {}

  bool filter(AVFrame *dst) override
  {
    bool eof;
    if (!fg.processFrame())
      throw ffmpeg::Exception("Post video filter produced no frame!.");
    out.pop(dst, &eof);
    return eof;
  }

  private:
  ffmpeg::filter::Graph fg;
  ffmpeg::AVFrameQueue<NullMutex, NullConditionVariable<NullMutex>,
                       NullUniqueLock<NullMutex>>
      out;
};

/**
 * \brief a FFmpeg audio filter to convert a video AVFrame to desired format &
 * orientation
 */
class mexFFmpegAudioPostOp : public ffmpeg::PostOpInterface
{
  public:
  mexFFmpegAudioPostOp(ffmpeg::IAVFrameSourceBuffer &src,
                       const AVSampleFormat samplefmt)
      : out(1)
  {
    // create filter graph
    std::ostringstream ssout;
    ssout << "[in]aformat=sample_fmts=" << av_get_sample_fmt_name(samplefmt)
          << "[out]";
    fg = ffmpeg::filter::Graph(ssout.str());

    // Link filter graph to the in/out buffers
    fg.assignSource(src, "in");
    fg.assignSink(out, "out");

    // finalize the filter configuration
    fg.configure();
  }
  ~mexFFmpegAudioPostOp() {}

  bool filter(AVFrame *dst) override
  {
    bool eof;
    if (!fg.processFrame())
      throw ffmpeg::Exception("Post video filter produced no frame!.");
    out.pop(dst, &eof);
    return eof;
  }

  private:
  ffmpeg::filter::Graph fg;
  ffmpeg::AVFrameQueue<NullMutex, NullConditionVariable<NullMutex>,
                       NullUniqueLock<NullMutex>>
      out;
};
