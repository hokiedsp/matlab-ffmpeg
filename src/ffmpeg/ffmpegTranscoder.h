#pragma once

#include "ffmpegOptionsContext.h"

#include "ffmpegPackage.h"
#include "ffmpegInputFile.h"
#include "ffmpegInputStream.h"
#include "ffmpegOutputFile.h"
#include "ffmpegOutputStream.h"
#include "ffmpegFilterGraph.h"

namespace ffmpeg
{
struct TranscodeOptionsContext : public OptionsContext
{
};

struct Transcoder : public ffmpegPackage
{
   const char *const forced_keyframes_const_names[];

   InputStreamRefs input_streams;
   InputFiles input_files;

   OutputStreamRefs output_streams;
   OutputFiles output_files;

   FilterGraphs filtergraphs;

   std::string vstats_filename;
   FILE *vstats_file;

   std::string sdp_filename;

   float audio_drift_threshold;
   float dts_delta_threshold;
   float dts_error_threshold;

   int audio_volume;
   int audio_sync_method;
   int video_sync_method;
   float frame_drop_threshold;
   int do_benchmark;
   int do_benchmark_all;
   int do_deinterlace;
   int do_hex_dump;
   int do_pkt_dump;
   int copy_ts;
   int start_at_zero;
   int copy_tb;
   int debug_ts;
   int exit_on_error;
   int abort_on_flags;
   int print_stats;
   int qp_hist;
   int stdin_interaction;
   int frame_bits_per_raw_sample;
   AVIOContext *progress_avio;
   float max_error_rate;
   char *videotoolbox_pixfmt;

   const OptionDefs options;
   const HWAccels hwaccels;
   int hwaccel_lax_profile_check;
   AVBufferRef *hw_device_ctx;

   volatile int received_sigterm;
   volatile int received_nb_signals;
   volatile bool transcode_init_done;
   // volatile int ffmpeg_exited;
   // int main_return_code;

   Transcoder();
   void transcode(void);

 private:
   int transcode_init(void);
   int transcode_step(void);
   
   bool need_output(void);

   void *input_thread(void *arg);
   void init_input_threads(void);
   void free_input_threads(void);

   void flush_encoders(void);

   InputStream *get_input_stream(OutputStream *ost);

   OutputStream *choose_output(void);

   bool got_eagain(void);
   void reset_eagain(void);
   int transcode_from_filter(FilterGraph &graph, InputStream *&best_ist);

   int reap_filters(int flush);
   
   static bool check_output_constraints(InputStream *ist, OutputStream *ost);
};
}
