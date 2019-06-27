#include "ffmpegStream.h"
#include "ffmpegException.h"

using namespace ffmpeg;

/**
 * \brief Class to manage AVStream
 */
BaseStream::BaseStream() : st(NULL), ctx(NULL)
{
}

BaseStream::~BaseStream()
{
  if (ctx)
    close();
}

// IMediaHandler interface functions
void BaseStream::setTimeBase(const AVRational &tb)
{
  if (!st)
    Exception("Cannot set time base; no AVStream open.");
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

void VideoStream::setMediaParams(const MediaParams &new_params)
{
  VideoHandler::setMediaParams(new_params); // may throw if type mismatch detected
  VideoParams &p = *dynamic_cast<VideoParams *>(params);

  BaseStream::setTimeBase(p.time_base);
  if (!ctx) return;

  ctx->pix_fmt = p.format;
  ctx->width = p.width;
  ctx->height = p.height;
  ctx->sample_aspect_ratio = p.sample_aspect_ratio;
}

void VideoStream::setTimeBase(const AVRational &tb)
{
  VideoHandler::setTimeBase(tb);
  BaseStream::setTimeBase(tb);
}

void VideoStream::setFormat(const AVPixelFormat fmt)
{
  VideoHandler::setFormat(fmt);
  if (ctx) ctx->pix_fmt = fmt;
}
void VideoStream::setWidth(const int w)
{
  VideoHandler::setWidth(w);
  if (ctx) ctx->width = w;
}
void VideoStream::setHeight(const int h)
{
  VideoHandler::setHeight(h);
  if (ctx) ctx->height = h;
}
void VideoStream::setSAR(const AVRational &sar)
{
  VideoHandler::setSAR(sar);
  if (ctx) ctx->sample_aspect_ratio = sar;
}

///////////////////////////////////////////////////////////////////////////////

// AUDIOSTREAM

void AudioStream::setMediaParams(const MediaParams &new_params)
{
  AudioHandler::setMediaParams(new_params); // may throw if type mismatch detected
  AudioParams &p = *dynamic_cast<AudioParams *>(params);

  BaseStream::setTimeBase(p.time_base);
  if (!ctx) return;

  ctx->sample_fmt = p.format;
  ctx->channel_layout = p.channel_layout;
  ctx->sample_rate = p.sample_rate;
}

void AudioStream::setTimeBase(const AVRational &tb)
{
  AudioHandler::setTimeBase(tb);
  BaseStream::setTimeBase(tb);
}

void AudioStream::setFormat(const AVSampleFormat fmt)
{
  AudioHandler::setFormat(fmt);
  if (ctx) ctx->sample_fmt = dynamic_cast<AudioParams *>(params)->format;
}
void AudioStream::setChannelLayout(const uint64_t layout)
{
  AudioHandler::setChannelLayout(layout);
  if (ctx) ctx->channel_layout = dynamic_cast<AudioParams *>(params)->channel_layout;
}
void AudioStream::setChannelLayoutByName(const std::string &name)
{
  AudioHandler::setChannelLayoutByName(name);
  if (ctx) ctx->channel_layout = dynamic_cast<AudioParams *>(params)->channel_layout;
}
void AudioStream::setSampleRate(const int fs)
{
  AudioHandler::setSampleRate(fs);
  if (ctx) ctx->sample_rate = dynamic_cast<AudioParams *>(params)->sample_rate;
}
