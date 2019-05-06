#pragma once

#include <vector>
#include <set>
#include <regex>

extern "C" {
#include <libavformat/avformat.h> // AVBufferRef
#include <libavutil/buffer.h>     // AVBufferRef
#include <libavformat/avio.h>     // AVIOInterruptCB
}

typedef std::set<std::string> unique_strings;

namespace ffmpeg
{

typedef std::vector<const AVInputFormat *> AVInputFormatPtrs;
typedef std::vector<const AVOutputFormat *> AVOutputFormatPtrs;

struct Base
{
   //  static int exit_on_error;
   //  static int audio_sync_method;
   //  static float audio_drift_threshold;
   //  static int copy_ts;
   //  static int start_at_zero;
   //  static AVBufferRef *hw_device_ctx;
   //  static AVIOInterruptCB int_cb;
   //  static int received_nb_signals;
   //  static int transcode_init_done;
   //  static bool input_stream_potentially_available;

   Base();
   ~Base();

   //  static int decode_interrupt_cb(void *ctx);

   static AVOutputFormatPtrs get_output_formats_devices(const AVMediaType type, const int flags);
   static AVInputFormatPtrs get_input_formats_devices(const AVMediaType type, const int flags);
 private:
   static int num_objs;

   static bool match_format_name(std::string name, const unique_strings &names);
   
   template <typename FormatPtrs>
   static unique_strings get_format_names(const FormatPtrs &fmtptrs)
   {
    unique_strings rval;
    std::regex comma_re(","); // whitespace
    for (FormatPtrs::const_iterator fmtptr = fmtptrs.begin(); fmtptr < fmtptrs.end(); fmtptr++)
    {
      std::string name = (*fmtptr)->name; // comma separated list of names of output container
      for (std::sregex_token_iterator it(name.begin(), name.end(), comma_re, -1); it != std::sregex_token_iterator(); it++)
        rval.insert(*it);
    }
    return rval;
  }
};
}
