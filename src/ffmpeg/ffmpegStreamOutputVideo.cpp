#include "ffmpegStreamOutput.h"

extern "C" {
// #include <libavformat/avformat.h>
// #include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
}

using namespace ffmpeg;

OutputVideoStream::OutputVideoStream(IAVFrameSource *buf) : OutputStream(buf), keep_pix_fmt(true)
{
}
OutputVideoStream::~OutputVideoStream() {}

AVPixelFormat OutputVideoStream::choose_pixel_fmt(AVPixelFormat target) const
{
  if (!ctx)
    return AV_PIX_FMT_NONE;

  const AVCodec *codec = getAVCodec();
  if (codec && codec->pix_fmts)
  {
    AVPixelFormats p;
    const AVPixelFormat *fmt = codec->pix_fmts;
    while (fmt)
      p.push_back(*fmt);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(target);
    int has_alpha = desc ? desc->nb_components % 2 == 0 : 0;
    enum AVPixelFormat best = AV_PIX_FMT_NONE;

    if (ctx->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL)
    {
      p = BaseStream::get_compliance_unofficial_pix_fmts(ctx->codec_id, p);
    }
    AVPixelFormats::iterator pfmt;
    for (pfmt = p.begin(); pfmt < p.end(); ++pfmt)
    {
      best = avcodec_find_best_pix_fmt_of_2(best, *pfmt, target, has_alpha, NULL);
      if (*pfmt == target)
        break;
    }
    if (pfmt == p.end())
    {
      if (target != AV_PIX_FMT_NONE)
        av_log(NULL, AV_LOG_WARNING,
               "Incompatible pixel format '%s' for codec '%s', auto-selecting format '%s'\n",
               av_get_pix_fmt_name(target),
               codec->name,
               av_get_pix_fmt_name(best));
      return best;
    }
  }
  return target;
}

AVPixelFormats OutputVideoStream::choose_pix_fmts() const
// static char *choose_pix_fmts(OutputFilter *ofilter)
{
  AVPixelFormats ret;

  // AVDictionaryEntry *strict_dict = av_dict_get(encoder_opts, "strict", NULL, 0);
  // if (strict_dict)
  //   // used by choose_pixel_fmt() and below
  //   av_opt_set(ctx, "strict", strict_dict->value, 0);

  // no change if keep_pix_fmt flag is set
  if (keep_pix_fmt)
  {
    ret.push_back(AV_PIX_FMT_NONE);
    return ret;
  }

  if (ctx->pix_fmt != AV_PIX_FMT_NONE)
  {
    ret.push_back(choose_pixel_fmt(ctx->pix_fmt));
    return ret;
  }

  return getPixelFormats();
}

AVPixelFormats OutputVideoStream::getPixelFormats() const
{
  AVPixelFormats ret;
  const AVCodec *enc = getAVCodec();
  if (enc && enc->pix_fmts)
  {
    for (const AVPixelFormat *p = enc->pix_fmts; *p != AV_PIX_FMT_NONE; p++)
      ret.push_back(*p);

    if (ctx->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL)
      ret = get_compliance_unofficial_pix_fmts(ctx->codec_id, ret);
  }
  return ret;
}
