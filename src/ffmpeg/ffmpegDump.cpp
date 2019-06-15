#include "ffmpegDump.h"

#include <iomanip>
#include <memory>
#include <sstream>

extern "C"
{
#include <libavutil/avstring.h>
#include <libavutil/opt.h>
}

#include "ffmpegPtrs.h"

namespace ffmpeg
{

void printFPS(std::ostringstream &sout, double d, const char *postfix)
{
  uint64_t v = lrintf(float(d * 100.0));
  if (!v)
    sout << std::ios_base::fixed << std::setw(1) << std::setprecision(4) << d;
  else if (v % 100)
    sout << std::ios_base::fixed << std::setw(3) << std::setprecision(2) << d;
  else if (v % (100 * 1000))
    sout << std::ios_base::fixed << std::setw(1) << std::setprecision(0) << d;
  else
    sout << std::ios_base::fixed << std::setw(1) << std::setprecision(0) << d << 'k';
  sout << ' ' << postfix;
}

void dumpMetadata(std::ostringstream &sout, AVDictionary *m, const char *indent)
{
  if (m && !(av_dict_count(m) == 1 && av_dict_get(m, "language", NULL, 0)))
  {
    AVDictionaryEntry *tag = NULL;

    sout << indent << "Metadata:" << std::endl;
    while ((tag = av_dict_get(m, "", tag, AV_DICT_IGNORE_SUFFIX)))
      if (strcmp("language", tag->key))
      {
        const char *p = tag->value;
        sout << indent << "  " << std::setw(16) << std::left << tag->key << ": ";
        while (*p)
        {
          char tmp[256];
          size_t len = strcspn(p, "\x8\xa\xb\xc\xd");
          av_strlcpy(tmp, p, FFMIN(sizeof(tmp), len + 1));
          sout << tmp;
          p += len;
          if (*p == 0xd) sout << ' ';
          if (*p == 0xa) sout << std::endl
                              << indent << "  " << std::setw(16) << std::left << "";
          if (*p) p++;
        }
        sout << std::endl;
      }
  }
}

std::string dumpStreamFormat(AVStream *st, bool is_output, int flags, char *separator)
{
  std::ostringstream sout;

  AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL, 0);

  /* the pid is an important information, so we display it */
  /* XXX: add a generic system */
  if (flags & AVFMT_SHOW_IDS)
    sout << "[0x" << std::hex << st->id << "]";
  if (lang)
    sout << "(" << lang->value << ")";
  if ((flags & AVFMT_SHOW_IDS) || lang) sout << ": ";

  // codec info
  {
    int ret;
    char buf[256];
    AVCodecContext *avctx = avcodec_alloc_context3(NULL);
    if (!avctx) return "";
    AVCodecCtxPtr avctx_cleanup(avctx, &delete_codec_ctx);

    ret = avcodec_parameters_to_context(avctx, st->codecpar);
    if (ret < 0) return "";

    // Fields which are missoutg from AVCodecParameters need to be taken from the AVCodecContext
    avctx->properties = st->codec->properties;
    avctx->codec = st->codec->codec;
    avctx->qmin = st->codec->qmin;
    avctx->qmax = st->codec->qmax;
    avctx->coded_width = st->codec->coded_width;
    avctx->coded_height = st->codec->coded_height;

    if (separator) av_opt_set(avctx, "dump_separator", separator, 0);
    avcodec_string(buf, sizeof(buf), avctx, is_output);
    sout << buf;
  }

  if (st->sample_aspect_ratio.num &&
      av_cmp_q(st->sample_aspect_ratio, st->codecpar->sample_aspect_ratio))
  {
    AVRational display_aspect_ratio;
    av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
              st->codecpar->width * (int64_t)st->sample_aspect_ratio.num,
              st->codecpar->height * (int64_t)st->sample_aspect_ratio.den,
              1024 * 1024);
    sout << ", SAR " << st->sample_aspect_ratio.num << ':' << st->sample_aspect_ratio.den
         << " DAR " << display_aspect_ratio.num << ':' << display_aspect_ratio.den;
  }

  if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
  {
    int fps = st->avg_frame_rate.den && st->avg_frame_rate.num;
    int tbr = st->r_frame_rate.den && st->r_frame_rate.num;
    int tbn = st->time_base.den && st->time_base.num;
    int tbc = st->codec->time_base.den && st->codec->time_base.num;

    if (fps || tbr || tbn || tbc)
      sout << separator;

    if (fps)
      printFPS(sout, av_q2d(st->avg_frame_rate), tbr || tbn || tbc ? "fps, " : "fps");
    if (tbr)
      printFPS(sout, av_q2d(st->r_frame_rate), tbn || tbc ? "tbr, " : "tbr");
    if (tbn)
      printFPS(sout, 1 / av_q2d(st->time_base), tbc ? "tbn, " : "tbn");
    if (tbc)
      printFPS(sout, 1 / av_q2d(st->codec->time_base), "tbc");
  }

  if (st->disposition & AV_DISPOSITION_DEFAULT) sout << " (default)";
  if (st->disposition & AV_DISPOSITION_DUB) sout << " (dub)";
  if (st->disposition & AV_DISPOSITION_ORIGINAL) sout << " (original)";
  if (st->disposition & AV_DISPOSITION_COMMENT) sout << " (comment)";
  if (st->disposition & AV_DISPOSITION_LYRICS) sout << " (lyrics)";
  if (st->disposition & AV_DISPOSITION_KARAOKE) sout << " (karaoke)";
  if (st->disposition & AV_DISPOSITION_FORCED) sout << " (forced)";
  if (st->disposition & AV_DISPOSITION_HEARING_IMPAIRED) sout << " (hearing impaired)";
  if (st->disposition & AV_DISPOSITION_VISUAL_IMPAIRED) sout << " (visual impaired)";
  if (st->disposition & AV_DISPOSITION_CLEAN_EFFECTS) sout << " (clean effects)";
  if (st->disposition & AV_DISPOSITION_DESCRIPTIONS) sout << " (descriptions)";
  if (st->disposition & AV_DISPOSITION_DEPENDENT) sout << " (dependent)";
  if (st->disposition & AV_DISPOSITION_STILL_IMAGE) sout << " (still image)";
  sout << std::endl;

  dumpMetadata(sout, st->metadata, "    ");

  // dump_sidedata(NULL, st, "    ");

  return sout.str();
}
} // namespace ffmpeg