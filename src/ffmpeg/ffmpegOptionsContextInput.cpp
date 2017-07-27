#include "ffmpegOptionsContextInput.h"

extern "C" {
#include <libavutil/intreadwrite.h>
}

#include "ffmpegUtil.h"

using namespace ffmpeg;

AVCodec *InputOptionsContext::choose_decoder(AVFormatContext *s, AVStream *st) const
{
   const std::string *opt_str;
   if ((opt_str = getspec<SpecifierOptsString, std::string>("codec", s, st)))
   {
      AVCodec *codec = find_decoder(*opt_str, st->codecpar->codec_type); // get codec object, throws exception if invalid codec name
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

void InputOptionsContext::parse(const OptionGroup &g)
{
   const std::string *opt_str;
   const SpecifierOptsInt *opt_specint;
   const SpecifierOptsString *opt_specstr;

   // do the common parsing operation
   OptionsContext::parse(g);

   // set format option dictionary according to user options (from ffmpeg_opt.c/open_input_file())

   // get user-specified input format
   if ((opt_str = get<OptionString, std::string>("f")) &&
       (!(file_iformat = av_find_input_format(opt_str->c_str()))))
      throw ffmpegException("Unknown input format: '" + *opt_str + "'");

   if (opt_specint = (const SpecifierOptsInt *)cfind("ar")) // audio_sample_rate
   {
      av_dict_set_int(&format_opts, "sample_rate", opt_specint->last->second, 0);
   }

   if (file_iformat && file_iformat->priv_class)
   {
      if ((opt_specint = (const SpecifierOptsInt *)cfind("ac")) && // audio_channels
          av_opt_find(&file_iformat->priv_class, "channels", NULL, 0, AV_OPT_SEARCH_FAKE_OBJ))
      {
         /* because we set audio_channels based on both the "ac" and
         * "channel_layout" options, we need to check that the specified
         * demuxer actually has the "channels" option before setting it */
         av_dict_set_int(&format_opts, "channels", opt_specint->last->second, 0);
      }
      if ((opt_specstr = (const SpecifierOptsString *)cfind("r")) &&
          av_opt_find(&file_iformat->priv_class, "framerate", NULL, 0, AV_OPT_SEARCH_FAKE_OBJ))
      {
         /* set the format-level framerate option;
         * this is important for video grabbers, e.g. x11 */
         av_dict_set(&format_opts, "framerate", opt_specstr->last->second.c_str(), 0);
      }
   }

   if (opt_str = get<OptionString,std::string>("s")) // frame_sizes
      av_dict_set(&format_opts, "video_size", opt_str->c_str(), 0);

   if (opt_str = get<OptionString,std::string>("pix_fmt")) // frame_pix_fmts
      av_dict_set(&format_opts, "pixel_format", opt_str->c_str(), 0);
}
