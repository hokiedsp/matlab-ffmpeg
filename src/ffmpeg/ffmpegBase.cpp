#include "ffmpegBase.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

using namespace ffmpeg;

int ffmpegBase::exit_on_error = 0;
int ffmpegBase::num_objs = 0;
int ffmpegBase::audio_sync_method = 0;
float ffmpegBase::audio_drift_threshold = (float)0.1;
int ffmpegBase::copy_ts = 0;
AVBufferRef *ffmpegBase::hw_device_ctx = NULL;
AVIOInterruptCB ffmpegBase::int_cb = { ffmpegBase::decode_interrupt_cb, NULL };
int ffmpegBase::received_nb_signals = 0;
int ffmpegBase::transcode_init_done = 0;
bool ffmpegBase::input_stream_potentially_available = false;
int ffmpegBase::start_at_zero     = 0;
int ffmpegBase::decode_interrupt_cb(void *ctx)
{
   return received_nb_signals > transcode_init_done;
}

ffmpegBase::ffmpegBase()
{
   if (num_objs++ == 0)
   {
      //init_dynload();

      av_register_all(); // Initialize libavformat and register all the muxers, demuxers and protocols.
      //av_log_set_flags(AV_LOG_SKIP_REPEATED);
      avcodec_register_all();
      // #if CONFIG_AVDEVICE
      // avdevice_register_all();
      // #endif
      // avfilter_register_all();
      // av_register_all();
      avformat_network_init(); // Do global initialization of network components
                               // #if CONFIG_AVDEVICE
                               //       avdevice_register_all(); // Initialize libavdevice and register all the input and output devices.
                               // #endif

      setvbuf(stderr, NULL, _IONBF, 0); /* win32 runtime needs this */
   }
}

ffmpegBase::~ffmpegBase()
{
   if (--num_objs == 0)
   {
      avformat_network_deinit(); // Undo the initialization done by avformat_network_init
   }
}
