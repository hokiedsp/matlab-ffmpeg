#include "ffmpegFilterSources.h"

#include "../ffmpegException.h"

#include "../ffmpegLogUtils.h"

extern "C" {
#include <libavfilter/buffersrc.h>
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
}

#include <sstream> // std::stringstream
#include <iomanip> // std::stringstream

using namespace ffmpeg;
using namespace ffmpeg::filter;

///////////////////////////////////////////////////////////
SourceBase::SourceBase(Graph &fg, IAVFrameSource &srcbuf) : EndpointBase(fg, srcbuf), buf(srcbuf) {}
SourceBase::~SourceBase() { av_log(NULL,AV_LOG_INFO,"destroyed SourceBase\n"); }

void SourceBase::link(AVFilterContext *other, const unsigned otherpad, const unsigned pad, const bool issrc)
{
  if (!issrc || pad > 0)
    throw ffmpegException("[SourceBase::link] Source filter does not have an input pad and has only 1 output pad.");

  EndpointBase::link(other, otherpad, prefilter_pad, issrc);
}

int SourceBase::processFrame()
{
  int ret = buf.tryToPop(frame); // if frame==NULL, eos
  if (ret == 0)
    ret = av_buffersrc_add_frame_flags(context, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
  av_frame_unref(frame);
  return ret;
}

/////////////////////////////

VideoSource::VideoSource(Graph &fg, IAVFrameSource &srcbuf)
    : SourceBase(fg, srcbuf), VideoHandler(dynamic_cast<IVideoHandler &>(srcbuf)), sws_flags(0) {}

AVFilterContext *VideoSource::configure(const std::string &name)
{
  // configure the filter
  create_context("buffer", name);

  // av_log(NULL, AV_LOG_ERROR, "video source context created...");

  // // also send in the hw_frame_ctx  (if given)
  // AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();
  // if (!par)
  //   throw ffmpegException("[ffmpeg::filter::VideoSource::configure] Failed during av_buffersrc_parameters_alloc() call.");
  // memset(par, 0, sizeof(*par));
  // par->format = AV_PIX_FMT_NONE;
  // // par->hw_frames_ctx = hw_frames_ctx;
  // int ret = av_buffersrc_parameters_set(context, par);
  // av_freep(&par);
  // if (ret < 0)
  //   throw ffmpegException("[ffmpeg::filter::VideoSource::configure] Failed to call av_buffersrc_parameters_set().");

  // av_log(NULL, AV_LOG_ERROR, "video source parameters created...");

  return configure_prefilter(true);
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

  std::string fmtstr = av_get_pix_fmt_name(format);
  std::stringstream sout;
  sout << "video_size=" << width << 'x' << height << ':'
       << "pix_fmt=" << fmtstr << ':'
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
/**
   * \brief Load media parameters from its buffer
   */
bool VideoSource::updateMediaParameters()
{
  IVideoHandler &vbuf = dynamic_cast<IVideoHandler &>(buf);
  time_base = buf.getTimeBase();
  setVideoParams(vbuf.getVideoParams());

  // check for validity
  if (format == AV_PIX_FMT_NONE || time_base.num <= 0 || time_base.den <= 0 ||
      width <= 0 || height <= 0 || sample_aspect_ratio.num <= 0 || sample_aspect_ratio.den <= 0)
    return false;

  // if filter context has already been set, propagate the parameters to it as well
  if (context)
  {
    std::unique_ptr<AVBufferSrcParameters, decltype(av_free) *> par(av_buffersrc_parameters_alloc(), av_free);
    if (!par)
      throw ffmpegException("[ffmpeg::filter::VideoSource::updateMediaParameters] Could not allocate AVBufferSrcParameters.");
    par->format = format;
    par->time_base = time_base;
    par->width = width;
    par->height = height;
    par->sample_aspect_ratio = sample_aspect_ratio;
    // par->frame_rate = {0, 0};  //AVRational
    par->hw_frames_ctx = NULL; // AVBufferRef *

    if (av_buffersrc_parameters_set(context, par.get() )< 0)
      throw ffmpegException("[ffmpeg::filter::VideoSource::updateMediaParameters] AVFilterContext could not accept parameters.");
  }
  return true;
};

/////////////////////////////
AudioSource::AudioSource(Graph &fg, IAVFrameSource &srcbuf)
    : SourceBase(fg, srcbuf),
      AudioHandler(dynamic_cast<IAudioHandler &>(srcbuf)) {}

AVFilterContext *AudioSource::configure(const std::string &name)
{
  // configure the AVFilterContext
  create_context("abuffer", name);
  return configure_prefilter(true);
}

std::string AudioSource::generate_args()
{
  std::stringstream sout;
  sout << "time_base=" << time_base.num << 'x' << time_base.den << ':'
       << "sample_fmt=" << av_get_sample_fmt_name(format) << ':'
       << "channel_layout=0x" << std::setbase(16) << channel_layout;
  return sout.str();
}

/**
   * \brief Load media parameters from its buffer
   */
bool AudioSource::updateMediaParameters()
{
  setTimeBase(buf.getTimeBase());
  setAudioParams(dynamic_cast<IAudioHandler &>(buf).getAudioParams());

  // validate the parameters
  if (format <= AV_SAMPLE_FMT_NONE || time_base.num <= 0 || time_base.den <= 0 ||
      sample_rate <= 0 ||channel_layout <= 0)
    return false;

  // if filter context has already been set, propagate the parameters to it as well
  if (context)
  {
    std::unique_ptr<AVBufferSrcParameters, decltype(av_free) *> par(av_buffersrc_parameters_alloc(), av_free);
    if (!par)
      throw ffmpegException("[ffmpeg::filter::AudioSource::updateMediaParameters] Could not allocate AVBufferSrcParameters.");

    par->format = format;
    par->time_base = time_base;
    par->sample_rate = sample_rate;
    par->channel_layout = channel_layout;
    if (av_buffersrc_parameters_set(context, par.get()) < 0)
      throw ffmpegException("[ffmpeg::filter::VideoSource::updateMediaParameters] AVFilterContext could not accept parameters.");
  }
  return true;
};
