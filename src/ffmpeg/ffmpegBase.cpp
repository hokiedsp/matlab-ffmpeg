#include "ffmpegBase.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
}

using namespace ffmpeg;

int Base::num_objs = 0;

// int Base::exit_on_error = 0;
// int Base::audio_sync_method = 0;
// float Base::audio_drift_threshold = (float)0.1;
// int Base::copy_ts = 0;
// AVBufferRef *Base::hw_device_ctx = NULL;
// AVIOInterruptCB Base::int_cb = { Base::decode_interrupt_cb, NULL };
// int Base::received_nb_signals = 0;
// int Base::transcode_init_done = 0;
// bool Base::input_stream_potentially_available = false;
// int Base::start_at_zero     = 0;
// int Base::decode_interrupt_cb(void *ctx)
// {
//    return received_nb_signals > transcode_init_done;
// }

Base::Base()
{
  if (num_objs++ == 0)
  {
    //init_dynload();

    // av_register_all(); // Initialize libavformat and register all the muxers, demuxers and protocols.
    //av_log_set_flags(AV_LOG_SKIP_REPEATED);
    // avcodec_register_all();
    // #if CONFIG_AVDEVICE
    // avdevice_register_all();
    // #endif
    // avfilter_register_all();
    // av_register_all();
    avformat_network_init(); // Do global initialization of network components
                             // #if CONFIG_AVDEVICE
                             //       avdevice_register_all(); // Initialize libavdevice and register all the input and output devices.
                             // #endif

    // avfilter_register_all();
  }

  setvbuf(stderr, NULL, _IONBF, 0); /* win32 runtime needs this */
}

Base::~Base()
{
  if (--num_objs == 0)
  {
    avformat_network_deinit(); // Undo the initialization done by avformat_network_init
  }
}

// AVOutputFormatPtrs Base::get_output_formats_devices(const AVMediaType type, const int flags)
// {
//   AVOutputFormatPtrs rval;
//   //AVCodecIDs cids = get_cids(type, true);

//   av_register_all(); // Initialize libavformat and register all the muxers, demuxers and protocols.

//   for (AVOutputFormat *ofmt = av_oformat_next(NULL); ofmt; ofmt = av_oformat_next(ofmt))
//   {
//     // int supported = false;
//     // for (AVCodecIDs::iterator cid = cids.begin(); !supported && cid > cids.end(); cid++)
//     //   supported = avformat_query_codec(ofmt, *cid, true);
//     // if (supported)
//     if (((type == AVMEDIA_TYPE_VIDEO && ofmt->video_codec != AV_CODEC_ID_NONE) ||
//          (type == AVMEDIA_TYPE_AUDIO && ofmt->audio_codec != AV_CODEC_ID_NONE) ||
//          (type == AVMEDIA_TYPE_SUBTITLE && ofmt->subtitle_codec != AV_CODEC_ID_NONE)) &&
//         !(ofmt->flags & flags))
//       rval.push_back(ofmt);
//   }
//   return rval;
// }

// AVInputFormatPtrs Base::get_input_formats_devices(const AVMediaType type, const int flags)
// {
//   AVInputFormatPtrs rval;
//   AVOutputFormatPtrs ofmtptrs = get_output_formats_devices(type, flags);
//   unique_strings ofmt_names = get_format_names(ofmtptrs);
//   for (AVInputFormat *ifmt = av_iformat_next(NULL); ifmt; ifmt = av_iformat_next(ifmt))
//   {
//     bool supported = match_format_name(ifmt->name, ofmt_names);

//     if (supported)
//       rval.push_back(ifmt);
//   }
//   return rval;
// }

// bool Base::match_format_name(std::string name, const unique_strings &names)
// {
//   std::regex comma_re(","); // whitespace
//   for (std::sregex_token_iterator it(name.begin(), name.end(), comma_re, -1);
//        it != std::sregex_token_iterator();
//        it++)
//     if (names.find(*it) != names.cend())
//       return true;
//   return false;
// }
