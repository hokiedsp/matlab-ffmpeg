#include "ffmpegFilterSources.h"

#include "../ffmpegException.h"

extern "C" {
 #include <libavfilter/buffersrc.h>
 #include <libswscale/swscale.h>
//  #include <libavutil/opt.h>
}

#include <sstream> // std::stringstream
#include <iomanip> // std::stringstream

using namespace ffmpeg;
using namespace ffmpeg::filter;

///////////////////////////////////////////////////////////
SourceBase::SourceBase(Graph &fg, IAVFrameSource &buf)
    : EndpointBase(fg, dynamic_cast<IMediaHandler&>(buf)), src(&buf)//, hw_frames_ctx(NULL)
{
}

void SourceBase::link(AVFilterContext *other, const unsigned otherpad, const unsigned pad, const bool issrc)
{
  if (!issrc || pad > 0)
    throw ffmpegException("[SourceBase::link] Source filter does not have an input pad and has only 1 output pad.");

  Base::link(other, otherpad, pad, issrc);
}

int SourceBase::processFrame()
{
  AVFrame *frame;
  if (!src)
    throw ffmpegException("[SourceBase::processFrame] AVFrame source buffer has not been set.");
  int ret = src->tryToPop(frame); // if frame==NULL, eos
  if (ret==0)
    ret = av_buffersrc_add_frame_flags(context, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
  return ret;
}

/////////////////////////////

VideoSource::VideoSource(Graph &fg, IAVFrameSource &buf)
    : SourceBase(fg, buf), VideoParams(dynamic_cast<IVideoHandler&>(buf).getVideoParams()), sws_flags(0)
{}

AVFilterContext *VideoSource::configure(const std::string &name)
{
  // configure the filter
  create_context("buffer", name);

  // also send in the hw_frame_ctx  (if given)
  AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();
  if (!par)
    throw ffmpegException("[ffmpeg::filter::VideoSource::configure] Failed during av_buffersrc_parameters_alloc() call.");
  memset(par, 0, sizeof(*par));
  par->format = AV_PIX_FMT_NONE;
  // par->hw_frames_ctx = hw_frames_ctx;
  int ret = av_buffersrc_parameters_set(context, par);
  av_freep(&par);
  if (ret < 0)
    throw ffmpegException("[ffmpeg::filter::VideoSource::configure] Failed to call av_buffersrc_parameters_set().");

  return context;
}

// void VideoSource::parameters_from_stream()
// {
//   // if not from AVStream, nothing to update
//   if (!st)
//     return;

//   InputVideoStream *ist = (InputVideoStream *)st;

//   width = ist->getWidth();
//   height = ist->getHeight();
//   time_base = ist->getTimeBase();
//   sample_aspect_ratio = ist->getSAR();
//   format = ist->getPixelFormat();
//   sws_flags = SWS_BILINEAR + (ist->getCodecFlags(AV_CODEC_FLAG_BITEXACT) ? SWS_BITEXACT : 0);
// }

std::string VideoSource::generate_args()
{
  // update object parameters with AVStream parameter
  // parameters_from_stream();

  // make sure SAR is valid
  if (!sample_aspect_ratio.den)
    sample_aspect_ratio = AVRational({0, 1});

  std::stringstream sout;
  sout << "video_size=" << width << 'x' << height << ':'
       << "pix_fmt=" << av_get_pix_fmt_name(format) << ':'
       << "time_base=" << time_base.num << '/' << time_base.den << ':'
       << "pixel_aspect=" << sample_aspect_ratio.num << '/' << sample_aspect_ratio.den << ':'
       << "sws_param=flags=" << sws_flags;

  return sout.str();
}

// void VideoSource::parameters_from_frame(const AVFrame *frame)
// {
//   av_buffer_unref(&hw_frames_ctx);

//   format = (AVPixelFormat)frame->format;
//   // time_base = frame->time_base;
//   width = frame->width;
//   height = frame->height;
//   sample_aspect_ratio = frame->sample_aspect_ratio;

//   if (frame->hw_frames_ctx)
//   {
//     hw_frames_ctx = av_buffer_ref(frame->hw_frames_ctx);
//     if (!hw_frames_ctx)
//       throw ffmpegException(AVERROR(ENOMEM));
//   }
// }

/////////////////////////////
AudioSource::AudioSource(Graph &fg, IAVFrameSource &buf)
    : SourceBase(fg, buf),
      AudioParams(dynamic_cast<IAudioHandler &>(buf).getAudioParams()) {}

AVFilterContext *AudioSource::configure(const std::string &name)
{
  // configure the AVFilterContext
  create_context("abuffer", name);
  return context;
}

std::string AudioSource::generate_args()
{
  std::stringstream sout;
  sout << "time_base=" << time_base.num << 'x' << time_base.den << ':'
       << "sample_fmt=" << av_get_sample_fmt_name(format) << ':';
  if (channel_layout)
    sout << "channel_layout=0x" << std::setbase(16) << channel_layout;
  else
    sout << "channels=" << channels;
  return sout.str();
}

// void AudioSource::parameters_from_frame(const AVFrame *frame)
// {
//   av_buffer_unref(&hw_frames_ctx);

//   format = (AVSampleFormat)frame->format;
//   // time_base = frame->time_base;
//   channels = frame->channels;
//   channel_layout = frame->channel_layout;

//   if (frame->hw_frames_ctx)
//   {
//     hw_frames_ctx = av_buffer_ref(frame->hw_frames_ctx);
//     if (!hw_frames_ctx)
//       throw ffmpegException(AVERROR(ENOMEM));
//   }
// }
