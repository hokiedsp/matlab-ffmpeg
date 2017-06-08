#pragma once

extern "C" {
#include <libavutil/buffer.h> // AVBufferRef
#include <libavformat/avio.h> // AVIOInterruptCB
}

namespace ffmpeg
{
struct ffmpegBase
{
   static int exit_on_error;
   static int audio_sync_method;
   static float audio_drift_threshold;
   static int copy_ts;
   static int start_at_zero;
   static AVBufferRef *hw_device_ctx;
   static AVIOInterruptCB int_cb;
   static int received_nb_signals;
   static int transcode_init_done;
   static bool input_stream_potentially_available;

   ffmpegBase();
   ~ffmpegBase();

   static int decode_interrupt_cb(void *ctx);

 private:
   static int num_objs;
};
}
