#include "ffmpegFilterSinks.h"

#include "../ffmpegException.h"
#include "ffmpegFilterGraph.h"

extern "C"
{
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
}

#include <iomanip>
#include <sstream> // std::stringstream

using namespace ffmpeg;
using namespace ffmpeg::filter;

///////////////////////////////////////////////////////////
SinkBase::SinkBase(Graph &fg, IAVFrameSinkBuffer &buf) : EndpointBase(fg), sink(&buf), ena(false), synced(false) {}
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
  if (!ena) return AVERROR_EOF;
  AVFrame *frame = sink->peekToPush();
  int ret = av_buffersink_get_frame(context, frame);
  ena = (ret != AVERROR_EOF);
  if (ena)
    sink->push(); // new frame already placed, complete the transaction
  else
    sink->push(nullptr); // push EOF
  return ret;
}

////////////////////////////////
VideoSink::VideoSink(Graph &fg, IAVFrameSinkBuffer &buf) : SinkBase(fg, buf) {}
AVFilterContext *VideoSink::configure(const std::string &name)
{ // configure the AVFilterContext
  create_context("buffersink", name);
  return SinkBase::configure();
}

bool VideoSink::sync()
{
  if (!context || context->inputs[0]) return synced;
  VideoParams &p = *static_cast<VideoParams*>(params);
  p.time_base = av_buffersink_get_time_base(context);
  p.format = (AVPixelFormat)av_buffersink_get_format(context);
  p.width = av_buffersink_get_w(context);
  p.height = av_buffersink_get_h(context);
  p.sample_aspect_ratio = av_buffersink_get_sample_aspect_ratio(context);
  synced = true;
  return synced;
}

////////////////////////////////
AudioSink::AudioSink(Graph &fg, IAVFrameSinkBuffer &buf) : SinkBase(fg, buf) {}
AVFilterContext *AudioSink::configure(const std::string &name)
{
  // configure the filter
  create_context("abuffersink", name);
  av_opt_set_int(context, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN);

  // clears ena flag
  return SinkBase::configure();
}

bool AudioSink::sync()
{
  if (!context || context->inputs[0]) return synced;
  AudioParams &p = *static_cast<AudioParams*>(params);

  p.time_base = av_buffersink_get_time_base(context);
  p.format = (AVSampleFormat)av_buffersink_get_format(context);
  p.channel_layout = av_buffersink_get_channel_layout(context);
  synced = true;
  return true;

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
