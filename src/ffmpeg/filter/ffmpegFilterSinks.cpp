#include "ffmpegFilterSinks.h"

#include "ffmpegFilterGraph.h"
#include "../ffmpegException.h"

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
}

#include <sstream> // std::stringstream
#include <iomanip>

using namespace ffmpeg;
using namespace ffmpeg::filter;

///////////////////////////////////////////////////////////
SinkBase::SinkBase(Graph &fg, IAVFrameSink &buf) : EndpointBase(fg, buf), sink(buf), ena(false) {}
SinkBase::~SinkBase()
{
  av_log(NULL, AV_LOG_INFO, "destroying SinkBase\n");
  av_log(NULL, AV_LOG_INFO, "destroyed SinkBase\n");
}

AVFilterContext *SinkBase::configure(const std::string &name)
{ // configure the AVFilterContext
  ena = true;
  return configure_prefilter(false);
}

void SinkBase::link(AVFilterContext *other, const unsigned otherpad, const unsigned pad, const bool issrc)
{
  if (issrc || pad > 0)
    throw ffmpegException("Sink filter does not have a input pad and has only one output pad.");

  EndpointBase::link(other, otherpad, prefilter_pad, issrc);
}

int SinkBase::processFrame()
{
  int ret = av_buffersink_get_frame(context, frame);
  bool eof = (ret == AVERROR_EOF);
  if (ret == 0 || eof)
  {
    sink.push(eof ? NULL : frame);
    if (eof)
      ena = false;
  }
  av_frame_unref(frame);
  return ret;
}

int SinkBase::processFrame(const std::chrono::milliseconds &rel_time)
{
  int ret = av_buffersink_get_frame(context, frame);
  bool eof = (ret == AVERROR_EOF);
  if (ena && (ret == 0 || eof))
  {
    sink.push(eof ? NULL : frame, rel_time);
    if (eof)
      ena = false;
  }
  av_frame_unref(frame);
  return ret;
}

////////////////////////////////
VideoSink::VideoSink(Graph &fg, IAVFrameSink &buf)
    : SinkBase(fg, buf), VideoHandler(dynamic_cast<IVideoHandler &>(buf)) { }
AVFilterContext *VideoSink::configure(const std::string &name)
{ // configure the AVFilterContext
  create_context("buffersink", name);
  return SinkBase::configure();
}

void VideoSink::sync()
{
  if (!context || context->inputs[0])
    throw ffmpegException("[ffmpeg::filter::VideoSink::sync] AVFilterContext not set or parameters not yet available.");

  format = (AVPixelFormat)av_buffersink_get_format(context);
  width = av_buffersink_get_w(context);
  height = av_buffersink_get_h(context);
  time_base = av_buffersink_get_time_base(context);
  sample_aspect_ratio = av_buffersink_get_sample_aspect_ratio(context);
}

////////////////////////////////
AudioSink::AudioSink(Graph &fg, IAVFrameSink &buf)
    : SinkBase(fg, buf), AudioHandler(dynamic_cast<IAudioHandler &>(buf)) {}
AVFilterContext *AudioSink::configure(const std::string &name)
{
  // configure the filter
  create_context("abuffersink", name);
  av_opt_set_int(context, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN);

  // clears ena flag
  return SinkBase::configure();
}

void AudioSink::sync()
{
  if (!context)
    throw ffmpegException("[ffmpeg::filter::VideoSink::sync] AVFilterContext not set.");
  
  format = (AVSampleFormat)av_buffersink_get_format(context);
  time_base = av_buffersink_get_time_base(context);
  channel_layout = av_buffersink_get_channel_layout(context);

  // if linked to a stream
  // if (st)
  // {
  //   const AVCodec *enc = st->getAVCodec();
  //   if (!enc)
  //     throw ffmpegException("Encoder (codec %s) not found for an output stream",
  //                           avcodec_get_name(st->getAVStream()->codecpar->codec_id));

  //   if (!(enc->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
  //     av_buffersink_set_frame_size(context, st->getCodecFrameSize());
  // }
}
