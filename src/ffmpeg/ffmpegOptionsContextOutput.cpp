#include "ffmpegOptionsContextInput.h"

extern "C" {
#include <libavutil/intreadwrite.h>
}

#include "ffmpegUtil.h"

using namespace ffmpeg;

AVCodec *OutputOptionsContext::choose_encoder(AVFormatContext *s, AVStream *st) const
{
   const std::string *opt_str;
   if ((opt_str = getspec<SpecifierOptsString, std::string>("codec", s, st)))
   {
      AVCodec *codec = find_encoder(*opt_str, st->codecpar->codec_type); // get codec object, throws exception if invalid codec name
      st->codecpar->codec_id = codec->id;                                // override the stream codec

      // also set the codec tag (FOURCC represented as an int)
      try // if FOURCC already given as a number
      {
         st->codecpar->codec_tag = std::stol(*opt_str);
      }
      catch (...) // else given as FOURCC string
      {
         st->codecpar->codec_tag = AV_RL32(opt_str->c_str());
      }

      return codec; // return the codec object
   }

   // get the codec as loaded for the stream
   return avcodec_find_decoder(st->codecpar->codec_id);
}

void OutputOptionsContext::parse(const OptionGroup &g)
{
   // const std::string *opt_str;
   // const SpecifierOptsInt *opt_specint;
   // const SpecifierOptsString *opt_specstr;

   // do the common parsing operation
   OptionsContext::parse(g);

   // set format option dictionary according to user options (from ffmpeg_opt.c/open_input_file())

   int64_t *t_val = get<OptionInt64,int64_t>("t"); // recording_time
   int64_t* to_val = get<OptionInt64,int64_t>("to"); // stop_time
   if (to_val && t_val)
   {
      opts.erase(find_option("to"));
      to_val = NULL;
      av_log(NULL, AV_LOG_WARNING, "-t and -to cannot be used together; using -t.\n");
   }
   else if (to_val && !t_val) // if stop_time given and recording_time not given
   {
      int64_t *ss_val = get<OptionInt64,int64_t)("ss"); // start_time
      int64_t tmp_start_time = (ss_val) ? *ss_val : 0;
      if (*to_val <= tmp_start_time)
         throw ffmpegException("-to value smaller than -ss; aborting.");

      *(OptionInt64*)*find_or_create_option(*find_optiondef("t")) = *to_val - tmp_start_time;
   }



   // // get user-specified input format
   // if ((opt_str = get<OptionString, std::string>("f")) &&
   //     (!(file_oformat = av_find_output_format(opt_str->c_str()))))
   //    throw ffmpegException("Unknown output format: '" + *opt_str + "'");

   // if (opt_specint = (const SpecifierOptsInt *)find("ar")) // audio_sample_rate
   // {
   //    av_dict_set_int(&format_opts, "sample_rate", opt_specint->last->second, 0);
   // }

   // if (file_iformat && file_iformat->priv_class)
   // {
   //    if ((opt_specint = (const SpecifierOptsInt *)find("ac")) && // audio_channels
   //        av_opt_find(&file_oformat->priv_class, "channels", NULL, 0, AV_OPT_SEARCH_FAKE_OBJ))
   //    {
   //       /* because we set audio_channels based on both the "ac" and
   //       * "channel_layout" options, we need to check that the specified
   //       * demuxer actually has the "channels" option before setting it */
   //       av_dict_set_int(&format_opts, "channels", opt_specint->last->second, 0);
   //    }
   //    if ((opt_specstr = (const SpecifierOptsString *)find("r")) &&
   //        av_opt_find(&file_iformat->priv_class, "framerate", NULL, 0, AV_OPT_SEARCH_FAKE_OBJ))
   //    {
   //       /* set the format-level framerate option;
   //       * this is important for video grabbers, e.g. x11 */
   //       av_dict_set(&format_opts, "framerate", opt_specstr->last->second.c_str(), 0);
   //    }
   // }

   // if (opt_str = get<OptionString,std::string>("s")) // frame_sizes
   //    av_dict_set(&format_opts, "video_size", opt_str->c_str(), 0);

   // if (opt_str = get<OptionString,std::string>("pix_fmt")) // frame_pix_fmts
   //    av_dict_set(&format_opts, "pixel_format", opt_str->c_str(), 0);
}
