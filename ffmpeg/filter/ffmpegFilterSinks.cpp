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
SinkBase::SinkBase(Graph &fg, IAVFrameSink &buf)
    : EndpointBase(fg, dynamic_cast<IMediaHandler &>(buf)), sink(&buf) {}

AVFilterContext *SinkBase::configure(const std::string &name)
{ // configure the AVFilterContext
  ena = true;
  return context;
}

void SinkBase::link(AVFilterContext *other, const unsigned otherpad, const unsigned pad, const bool issrc)
{
  if (issrc || pad > 0)
    throw ffmpegException("Sink filter does not have a input pad and has only one output pad.");

  Base::link(other, otherpad, pad, issrc);
}

int SinkBase::processFrame()
{
  if (!sink)
    throw ffmpegException("[SinkBase::processFrame] AVFrame sink buffer has not been set.");

  AVFrame *frame = NULL;
  int ret = av_buffersink_get_frame(context, frame);
  bool eof = (ret != AVERROR_EOF);
  if (ret == 0 || eof)
  {
    sink->push(eof ? NULL : frame);
    if (eof)
      ena = false;
  }

  return ret;
}

////////////////////////////////
VideoSink::VideoSink(Graph &fg, IAVFrameSink &buf)
    : SinkBase(fg, buf), VideoParams(dynamic_cast<IVideoHandler &>(buf).getVideoParams()) { }
AVFilterContext *VideoSink::configure(const std::string &name)
{ // configure the AVFilterContext
  create_context("buffersink", name);
  SinkBase::configure();
  return context;
}

void VideoSink::sync()
{
  format = (AVPixelFormat)av_buffersink_get_format(context);
  width = av_buffersink_get_w(context);
  height = av_buffersink_get_h(context);
  time_base = av_buffersink_get_time_base(context);
  sample_aspect_ratio = av_buffersink_get_sample_aspect_ratio(context);
}

////////////////////////////////
AudioSink::AudioSink(Graph &fg, IAVFrameSink &buf)
    : SinkBase(fg, buf), AudioParams(dynamic_cast<IAudioHandler &>(buf).getAudioParams()) {}
AVFilterContext *AudioSink::configure(const std::string &name)
{
  // clears ena flag
  SinkBase::configure();

  // configure the filter
  create_context("abuffersink", name);
  av_opt_set_int(context, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN);
  return context;
}

void AudioSink::sync()
{
  format = (AVSampleFormat)av_buffersink_get_format(context);
  time_base = av_buffersink_get_time_base(context);
  channel_layout = av_buffersink_get_channel_layout(context);
  channels = av_buffersink_get_channels(context);

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
