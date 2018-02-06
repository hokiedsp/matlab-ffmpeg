#include "ffmpegStream.h"
#include "ffmpegException.h"

using namespace ffmpeg;

/**
 * \brief Class to manage AVStream
 */
BaseStream::BaseStream() : st(NULL), ctx(NULL), pts(0), bparams({AVMEDIA_TYPE_UNKNOWN, {0, 0}})
{}

BaseStream::~BaseStream()
{
  if (ctx) close();
}

bool BaseStream::ready() { return ctx; }

void BaseStream::close()
{
  // if no stream is associated, nothing to do
  if (!ctx) return;

  // free up the context
  avcodec_free_context(&ctx);

  st->discard = AVDISCARD_ALL;
  st = NULL;
  ctx = NULL;

  // clear the basic media parameters
  bparams = {AVMEDIA_TYPE_UNKNOWN, {0, 0}};
}

int BaseStream::reset()
{
  return avcodec_send_packet(ctx, NULL);
}

///////////////////////////////////////////////////////

AVStream *BaseStream::getAVStream() const { return st; }
int BaseStream::getId() const { return st?st->index : -1; }
AVMediaType BaseStream::getMediaType() const { return ctx ? ctx->codec_type : AVMEDIA_TYPE_UNKNOWN; }
std::string BaseStream::getMediaTypeString() const { return av_get_media_type_string(getMediaType()); }
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

AVRational BaseStream::getTimeBase() const { return (st)?st->time_base:AVRational({0,0}); }
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
  if (!st) return;
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
