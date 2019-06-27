#include "ffmpegFilterSources.h"

#include "../ffmpegException.h"

#include "../ffmpegLogUtils.h"

extern "C"
{
#include <libavfilter/buffersrc.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <iomanip> // std::stringstream
#include <sstream> // std::stringstream

using namespace ffmpeg;
using namespace ffmpeg::filter;

///////////////////////////////////////////////////////////
SourceBase::SourceBase(Graph &fg, IAVFrameSourceBuffer &srcbuf) : EndpointBase(fg), buf(&srcbuf), eof(false) {}
SourceBase::~SourceBase() { av_log(NULL, AV_LOG_INFO, "destroyed SourceBase\n"); }

void SourceBase::link(AVFilterContext *other, const unsigned otherpad, const unsigned pad, const bool issrc)
{
  if (!issrc || pad > 0)
    throw Exception("[SourceBase::link] Source filter does not have an input pad and has only 1 output pad.");

  EndpointBase::link(other, otherpad, prefilter_pad, issrc);
}

int SourceBase::processFrame()
{
  if (eof) return AVERROR_EOF;       // if eof already encountered, do nothing
  AVFrame *frame = buf->peekToPop(); // blocks till new frame is in then grab the next frame to output (null if eof)
  int ret = av_buffersrc_add_frame_flags(context, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
  if (!frame) eof = true;
  buf->pop(); // pop the frame from the buffer
  return ret;
}

/////////////////////////////

VideoSource::VideoSource(Graph &fg, IAVFrameSourceBuffer &srcbuf)
    : SourceBase(fg, srcbuf), sws_flags(0) {}

AVFilterContext *VideoSource::configure(const std::string &name)
{
  // configure the filter
  create_context("buffer", name);

  // av_log(NULL, AV_LOG_ERROR, "video source context created...");

  // // also send in the hw_frame_ctx  (if given)
  // AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();
  // if (!par)
  //   throw Exception("[ffmpeg::filter::VideoSource::configure] Failed during av_buffersrc_parameters_alloc() call.");
  // memset(par, 0, sizeof(*par));
  // par->format = AV_PIX_FMT_NONE;
  // // par->hw_frames_ctx = hw_frames_ctx;
  // int ret = av_buffersrc_parameters_set(context, par);
  // av_freep(&par);
  // if (ret < 0)
  //   throw Exception("[ffmpeg::filter::VideoSource::configure] Failed to call av_buffersrc_parameters_set().");

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
  const VideoParams &p = static_cast<const VideoParams &>(buf->getMediaParams());

  std::string fmtstr = av_get_pix_fmt_name(p.format);
  std::stringstream sout;
  sout << "video_size=" << p.width << 'x' << p.height << ':'
       << "pix_fmt=" << fmtstr << ':'
       << "time_base=" << p.time_base.num << '/' << p.time_base.den << ':'
       << "pixel_aspect=" << p.sample_aspect_ratio.num << '/' << p.sample_aspect_ratio.den << ':'
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
//       throw Exception(AVERROR(ENOMEM));
//   }
// }
/**
   * \brief Load media parameters from its buffer
   */
bool VideoSource::updateMediaParameters()
{
  // must have context to update
  if (!context) return false;

  const VideoParams &p = static_cast<const VideoParams &>(buf->getMediaParams());

  // check for validity
  if (p.format == AV_PIX_FMT_NONE || p.time_base.num <= 0 || p.time_base.den <= 0 ||
      p.width <= 0 || p.height <= 0 || p.sample_aspect_ratio.num <= 0 || p.sample_aspect_ratio.den <= 0)
    return false;

  // if filter context has already been set, propagate the parameters to it as well
  std::unique_ptr<AVBufferSrcParameters, decltype(av_free) *> par(av_buffersrc_parameters_alloc(), av_free);
  if (!par)
    throw Exception("[ffmpeg::filter::VideoSource::updateMediaParameters] Could not allocate AVBufferSrcParameters.");
  par->format = p.format;
  par->time_base = p.time_base;
  par->width = p.width;
  par->height = p.height;
  par->sample_aspect_ratio = p.sample_aspect_ratio;
  // par->frame_rate = {0, 0};  //AVRational
  par->hw_frames_ctx = NULL; // AVBufferRef *

  if (av_buffersrc_parameters_set(context, par.get()) < 0)
    throw Exception("[ffmpeg::filter::VideoSource::updateMediaParameters] AVFilterContext could not accept parameters.");

  return true;
};

/////////////////////////////
AudioSource::AudioSource(Graph &fg, IAVFrameSourceBuffer &srcbuf)
    : SourceBase(fg, srcbuf){}

AVFilterContext *AudioSource::configure(const std::string &name)
{
  // configure the AVFilterContext
  create_context("abuffer", name);
  return configure_prefilter(true);
}

std::string AudioSource::generate_args()
{
  const AudioParams &p = static_cast<const AudioParams &>(buf->getMediaParams());

  std::stringstream sout;
  sout << "time_base=" << p.time_base.num << 'x' << p.time_base.den << ':'
       << "sample_fmt=" << av_get_sample_fmt_name(p.format) << ':'
       << "channel_layout=0x" << std::setbase(16) << p.channel_layout;
  return sout.str();
}

/**
   * \brief Load media parameters from its buffer
   */
bool AudioSource::updateMediaParameters()
{
    // must have context to update
  if (!context) return false;

  const AudioParams &p = static_cast<const AudioParams &>(buf->getMediaParams());

  // validate the parameters
  if (p.format <= AV_SAMPLE_FMT_NONE || p.time_base.num <= 0 || p.time_base.den <= 0 ||
      p.sample_rate <= 0 || p.channel_layout <= 0)
    return false;

  // if filter context has already been set, propagate the parameters to it as well
  if (context)
  {
    std::unique_ptr<AVBufferSrcParameters, decltype(av_free) *> par(av_buffersrc_parameters_alloc(), av_free);
    if (!par)
      throw Exception("[ffmpeg::filter::AudioSource::updateMediaParameters] Could not allocate AVBufferSrcParameters.");

    par->format = p.format;
    par->time_base = p.time_base;
    par->sample_rate = p.sample_rate;
    par->channel_layout = p.channel_layout;
    if (av_buffersrc_parameters_set(context, par.get()) < 0)
      throw Exception("[ffmpeg::filter::VideoSource::updateMediaParameters] AVFilterContext could not accept parameters.");
  }
  return true;
};
