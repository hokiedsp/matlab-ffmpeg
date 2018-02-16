#include "ffmpegStream.h"
#include "ffmpegException.h"

using namespace ffmpeg;

/**
 * \brief Class to manage AVStream
 */
BaseStream::BaseStream() : st(NULL), ctx(NULL), pts(0)
{
}

BaseStream::~BaseStream()
{
  if (ctx)
    close();
}

// IMediaHandler interface functions
BasicMediaParams BaseStream::getBasicMediaParams() const { return {getMediaType(), getTimeBase()}; }
AVMediaType BaseStream::getMediaType() const { return ctx ? ctx->codec_type : AVMEDIA_TYPE_UNKNOWN; }
std::string BaseStream::getMediaTypeString() const { return ctx ? av_get_media_type_string(ctx->codec_type):"unknown"; }
AVRational BaseStream::getTimeBase() const { return (st) ? st->time_base : (ctx) ? ctx->time_base : AVRational({0, 0}); }
void BaseStream::setTimeBase(const AVRational &tb)
{
  if (!st)
    ffmpegException("Cannot set time base; no AVStream open.");
  st->time_base = tb;
  if (ctx)
    ctx->time_base = tb;
}

bool BaseStream::ready() { return ctx; }

void BaseStream::close()
{
  // if no stream is associated, nothing to do
  if (!ctx)
    return;

  // free up the context
  avcodec_free_context(&ctx);

  st = NULL;
  ctx = NULL;
}

int BaseStream::reset()
{
  return avcodec_send_packet(ctx, NULL);
}

///////////////////////////////////////////////////////

AVStream *BaseStream::getAVStream() const { return st; }
int BaseStream::getId() const { return st ? st->index : -1; }

const AVCodec *BaseStream::getAVCodec() const { return ctx ? ctx->codec : NULL; }
std::string BaseStream::getCodecName() const
{
  return (ctx && ctx->codec && ctx->codec->name) ? ctx->codec->name : "";
}
std::string BaseStream::getCodecDescription() const
{
  return (ctx && ctx->codec && ctx->codec->long_name) ? ctx->codec->long_name : "";
}
bool BaseStream::getCodecFlags(const int mask) const { return ctx->flags & mask; }

int BaseStream::getCodecFrameSize() const { return ctx ? ctx->frame_size : 0; }

int64_t BaseStream::getLastFrameTimeStamp() const { return pts; }

////////////////////////////

// following comes from ffmpeg_filter.c

const AVPixelFormats BaseStream::get_compliance_unofficial_pix_fmts(AVCodecID codec_id, const AVPixelFormats default_formats)
{
  static const AVPixelFormats mjpeg_formats(
      {AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
       AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
       AV_PIX_FMT_NONE});
  static const AVPixelFormats ljpeg_formats(
      {AV_PIX_FMT_BGR24, AV_PIX_FMT_BGRA, AV_PIX_FMT_BGR0,
       AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P,
       AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P,
       AV_PIX_FMT_NONE});

  if (codec_id == AV_CODEC_ID_MJPEG)
  {
    return mjpeg_formats;
  }
  else if (codec_id == AV_CODEC_ID_LJPEG)
  {
    return ljpeg_formats;
  }
  else
  {
    return default_formats;
  }
}

void BaseStream::choose_sample_fmt()
{
  if (!st)
    return;
  const AVCodec *codec = getAVCodec();

  if (codec && codec->sample_fmts)
  {
    const enum AVSampleFormat *p = codec->sample_fmts;
    for (; *p != -1; p++)
    {
      if (*p == st->codecpar->format)
        break;
    }
    if (*p == -1)
    {
      if ((codec->capabilities & AV_CODEC_CAP_LOSSLESS) && av_get_sample_fmt_name((AVSampleFormat)st->codecpar->format) > av_get_sample_fmt_name(codec->sample_fmts[0]))
        av_log(NULL, AV_LOG_ERROR, "Conversion will not be lossless.\n");
      if (av_get_sample_fmt_name((AVSampleFormat)st->codecpar->format))
        av_log(NULL, AV_LOG_WARNING,
               "Incompatible sample format '%s' for codec '%s', auto-selecting format '%s'\n",
               av_get_sample_fmt_name((AVSampleFormat)st->codecpar->format),
               codec->name,
               av_get_sample_fmt_name(codec->sample_fmts[0]));
      st->codecpar->format = codec->sample_fmts[0];
    }
  }
}

// VIDEOSTREAM

void VideoStream::setVideoParams(const VideoParams &params)
{
  if (!ctx)
    throw ffmpegException("Stream codec is not set.");
  ctx->pix_fmt = params.format;
  ctx->width = params.width;
  ctx->height = params.height;
  ctx->sample_aspect_ratio = params.sample_aspect_ratio;
}

void VideoStream::setValidVideoParams(const VideoParams &params)
{
  if (!ctx)
    throw ffmpegException("Stream codec is not set.");
  if (params.format!=AV_PIX_FMT_NONE) ctx->pix_fmt = params.format;
  if (params.width>0) ctx->width = params.width;
  if (params.height>0) ctx->height = params.height;
  if (params.sample_aspect_ratio.num > 0 && params.sample_aspect_ratio.den > 0)
    ctx->sample_aspect_ratio = params.sample_aspect_ratio;
}

void VideoStream::setVideoParams(const IVideoHandler &other)
{
  setVideoParams(other.getVideoParams());
}

void VideoStream::setValidVideoParams(const IVideoHandler &other)
{
  setValidVideoParams(other.getVideoParams());
}

void VideoStream::setFormat(const AVPixelFormat fmt)
{
  if (!ctx)
    throw ffmpegException("Stream codec is not set.");
  ctx->pix_fmt = fmt;
}
void VideoStream::setWidth(const int w)
{
  if (!ctx)
    throw ffmpegException("Stream codec is not set.");
  ctx->width = w;
}
void VideoStream::setHeight(const int h)
{
  if (!ctx)
    throw ffmpegException("Stream codec is not set.");
  ctx->height = h;
}
void VideoStream::setSAR(const AVRational &sar)
{
  if (!ctx)
    throw ffmpegException("Stream codec is not set.");
  ctx->sample_aspect_ratio = sar;
}

//AUDIOSTREAM
std::string AudioStream::getChannelLayoutName() const
{
  if (!ctx)
    return "";

  int nb_channels = av_get_channel_layout_nb_channels(ctx->channel_layout);
  if (nb_channels)
  {
    char buf[1024];
    av_get_channel_layout_string(buf, 1024, nb_channels, ctx->channel_layout);
    return buf;
  }
  else
    return "";
}
void AudioStream::setAudioParams(const AudioParams &params)
{
  if (!ctx)
    throw ffmpegException("Stream codec is not set.");
  ctx->sample_fmt = params.format;
  ctx->channel_layout = params.channel_layout;
  ctx->sample_rate = params.sample_rate;
}
void AudioStream::setValidAudioParams(const AudioParams &params)
{
  if (!ctx)
    throw ffmpegException("Stream codec is not set.");
  if (params.format != AV_SAMPLE_FMT_NONE)
    ctx->sample_fmt = params.format;
  if (!params.channel_layout)
    ctx->channel_layout = params.channel_layout;
  if (params.sample_rate > 0)
    ctx->sample_rate = params.sample_rate;
}
void AudioStream::setAudioParams(const IAudioHandler &other)
{
  setAudioParams(other.getAudioParams());
}
void AudioStream::setValidAudioParams(const IAudioHandler &other)
{
  setValidAudioParams(other.getAudioParams());
}
void AudioStream::setFormat(const AVSampleFormat fmt)
{
  if (!ctx)
    throw ffmpegException("Stream codec is not set.");
  ctx->sample_fmt = fmt;
}
void AudioStream::setChannelLayout(const uint64_t layout)
{
  if (!ctx)
    throw ffmpegException("Stream codec is not set.");
  ctx->channel_layout = layout;
}
void AudioStream::setChannelLayoutByName(const std::string &name)
{
  if (!ctx)
    throw ffmpegException("Stream codec is not set.");

  ctx->channel_layout = av_get_channel_layout(name.c_str());
}
void AudioStream::setSampleRate(const int fs)
{
  if (!ctx)
    throw ffmpegException("Stream codec is not set.");
  ctx->sample_rate = fs;
}
