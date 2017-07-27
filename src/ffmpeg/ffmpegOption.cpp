#include "ffmpegOption.h"

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

using namespace ffmpeg;

AvOptionGroup::AvOptionGroup()
    : codec_opts(NULL), format_opts(NULL), sws_dict(NULL), swr_opts(NULL)
{
   av_dict_set(&sws_dict, "flags", "bicubic", 0);
}

AvOptionGroup::~AvOptionGroup()
{
   av_dict_free(&codec_opts);
   av_dict_free(&format_opts);
   av_dict_free(&sws_dict);
   av_dict_free(&swr_opts);
}

#define FLAGS (o->type == AV_OPT_TYPE_FLAGS && (arg[0] == '-' || arg[0] == '+')) ? AV_DICT_APPEND : 0
int AvOptionGroup::opt_default(const std::string &opt, const std::string &arg)
{
   const AVClass *cc = avcodec_get_class();
   const AVClass *fc = avformat_get_class();
   // #if CONFIG_AVRESAMPLE
   //    const AVClass *rc = avresample_get_class();
   // #endif
   // #if CONFIG_SWSCALE
   const AVClass *sc = sws_get_class();
   // #endif
   // #if CONFIG_SWRESAMPLE
   const AVClass *swr_class = swr_get_class();
   // #endif

   int consumed = 0;
   if (opt == "debug" || opt == "fdebug")
      av_log_set_level(AV_LOG_DEBUG);

   std::string::size_type p = opt.find(':');                                       // if not found, std::string::npos
   std::string opt_stripped = p == std::string::npos ? opt : opt.substr(0, p - 1); // grab the lhs of ':'
   const AVOption *o;

   if ((o = opt_find(&cc, opt_stripped, NULL, 0, AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ)) || ((opt[0] == 'v' || opt[0] == 'a' || opt[0] == 's') && (o = opt_find(&cc, opt.substr(1), NULL, 0, AV_OPT_SEARCH_FAKE_OBJ))))
   {
      av_dict_set(&codec_opts, opt.c_str(), arg.c_str(), FLAGS);
      consumed = 1;
   }
   if ((o = opt_find(&fc, opt, NULL, 0, AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ)))
   {
      av_dict_set(&format_opts, opt.c_str(), arg.c_str(), FLAGS);
      if (consumed)
         av_log(NULL, AV_LOG_VERBOSE, "Routing option %s to both codec and muxer layer\n", opt);
      consumed = 1;
   }

   if (!consumed && (o = opt_find(&sc, opt, NULL, 0, AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ)))
   {
      struct SwsContext *sws = sws_alloc_context();
      int ret = av_opt_set(sws, opt.c_str(), arg.c_str(), 0);
      sws_freeContext(sws);
      if (opt == "srcw" || opt == "srch" || opt == "dstw" || opt == "dsth" || opt == "src_format" || opt == "dst_format")
      {
         av_log(NULL, AV_LOG_ERROR, "Directly using swscale dimensions/format options is not supported, please use the -s or -pix_fmt options\n");
         return AVERROR(EINVAL);
      }
      if (ret < 0)
      {
         av_log(NULL, AV_LOG_ERROR, "Error setting option %s.\n", opt.c_str());
         return ret;
      }

      av_dict_set(&sws_dict, opt.c_str(), arg.c_str(), FLAGS);

      consumed = 1;
   }

   if (!consumed && (o = opt_find(&swr_class, opt, NULL, 0, AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ)))
   {
      struct SwrContext *swr = swr_alloc();
      int ret = av_opt_set(swr, opt.c_str(), arg.c_str(), 0);
      swr_free(&swr);
      if (ret < 0)
      {
         av_log(NULL, AV_LOG_ERROR, "Error setting option %s.\n", opt.c_str());
         return ret;
      }
      av_dict_set(&swr_opts, opt.c_str(), arg.c_str(), FLAGS);
      consumed = 1;
   }

   if (consumed)
      return 0;
   return AVERROR_OPTION_NOT_FOUND;
}

const AVOption *AvOptionGroup::opt_find(void *obj, const std::string &name, const std::string &unit, int opt_flags, int search_flags) const
{
   const AVOption *o = av_opt_find(obj, name.c_str(), unit.c_str(), opt_flags, search_flags);
   if (o && !o->flags)
      return NULL;
   return o;
}

std::vector<DictPtr> AvOptionGroup::find_stream_info(AVFormatContext *ic)
{
    /* Set AVCodecContext options for avformat_find_stream_info */
  std::vector<DictPtr> opts = setup_find_stream_info_opts(ic);

   // create array of raw pointers
   AVDictionary **opts_array = new AVDictionary *[opts.size()];
   for (int i = 0; i < opts.size(); i++)
      opts_array[i] = opts[i].get();

   /* If not enough info to get the stream parameters, we decode the
       first frames to get it. (used in mpeg case for example) */
   if (avformat_find_stream_info(ic, opts_array) < 0 && ic->nb_streams == 0)
      throw ffmpegException("Could not find codec parameters.");

   return opts;
}

std::vector<DictPtr> AvOptionGroup::setup_find_stream_info_opts(AVFormatContext *s)
{
   std::vector<DictPtr> opts;
   opts.reserve(s->nb_streams);

   for (unsigned int i = 0; i < s->nb_streams; i++)
      opts.emplace_back(filter_codec_opts(s->streams[i]->codecpar->codec_id, s, s->streams[i], NULL),delete_dict);

   return opts;
}

// AVDictionary *filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id, AVFormatContext *s, AVStream *st, AVCodec *codec) in cmdutils.cpp
AVDictionary *AvOptionGroup::filter_codec_opts(AVCodecID codec_id, AVFormatContext *s, AVStream *st, AVCodec *codec) const
{
   AVDictionary *ret = NULL;
   AVDictionaryEntry *t = NULL;
   int flags = s->oformat ? AV_OPT_FLAG_ENCODING_PARAM : AV_OPT_FLAG_DECODING_PARAM;
   char prefix = 0;
   const AVClass *cc = avcodec_get_class();

   if (!codec)
      codec = s->oformat ? avcodec_find_encoder(codec_id) : avcodec_find_decoder(codec_id);

   switch (st->codecpar->codec_type)
   {
   case AVMEDIA_TYPE_VIDEO:
      prefix = 'v';
      flags |= AV_OPT_FLAG_VIDEO_PARAM;
      break;
   case AVMEDIA_TYPE_AUDIO:
      prefix = 'a';
      flags |= AV_OPT_FLAG_AUDIO_PARAM;
      break;
   case AVMEDIA_TYPE_SUBTITLE:
      prefix = 's';
      flags |= AV_OPT_FLAG_SUBTITLE_PARAM;
      break;
   }

   while (t = av_dict_get(codec_opts, "", t, AV_DICT_IGNORE_SUFFIX))
   {
      char *p = strchr(t->key, ':');

      /* check stream specification in opt name */
      if (p)
      {
         switch (avformat_match_stream_specifier(s, st, p + 1))
         {
         case 1:
            *p = 0;
            break;
         case 0:
            continue;
         default:
            std::ostringstream msg;
            msg << "Invalid stream specifier: " << p + 1 << ".";
            throw ffmpegException(msg.str());
         }
      }

      if (av_opt_find(&cc, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) ||
          !codec || (codec->priv_class && av_opt_find(&codec->priv_class, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ)))
         av_dict_set(&ret, t->key, t->value, 0);
      else if (t->key[0] == prefix && av_opt_find(&cc, t->key + 1, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ))
         av_dict_set(&ret, t->key + 1, t->value, 0);

      if (p)
         *p = ':';
   }
   return ret;
}

////////////////////////////////////////////

void OptionGroup::finalize(const OptionGroupDef *d, const std::string &a)
{
   // make sure
   for (auto o = opts.begin(); o < opts.end(); o++)
   {
      if (d->flags && !(d->flags & o->opt.flags))
      {
         std::ostringstream msg;
         msg << "Option " << o->key << " (" << o->opt.help << ") cannot be applied to " << d->name << " " << a
             << " -- you are trying to apply an input option to an output file or vice versa. Move this option before the file it belongs to.";
         throw ffmpegException(msg.str());
      }
   }

   def = d;
   arg = a;
   valid = true;
}

///////////////////////////////////////////////////////////////////////////

double Option::parse_number(const std::string str)
{
   char *tail;
   double d = av_strtod(str.c_str(), &tail);
   if (*tail)
   {
      std::ostringstream msg;
      msg << "Expected number for " << name() << " but found: " << str;
      throw ffmpegException(msg.str());
   }
   return d;
}
