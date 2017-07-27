#pragma once

#include <thread>

extern "C" {
#include <libavformat/avformat.h>    // for AVFormatContext
#include <libavutil/rational.h>      // for AVRational
#include <libavutil/threadmessage.h> // for AVThreadMessageQueue
}

#include "ffmpegBase.h"
#include "ffmpegOptionsContextInput.h"
#include "ffmpegInputStream.h"
#include "ffmpegPtrs.h"

namespace ffmpeg
{
struct InputFile : public ffmpegBase
{
   InputFile(const std::string &filename, InputOptionsContext &o, const int i=0);
   ~InputFile();

   void seek(const int64_t timestamp);

   InputStreams streams; // streams
   FormatCtxPtr ctx;
   int index;

   int get_packet(AVPacket &pkt);
   void prepare_packet(AVPacket &pkt, InputStream *&ist);

   void init_thread(void);
   void free_thread(void);

   int64_t get_duration() const { return duration; }
   const AVRational &get_time_base() const { return time_base; }

       int64_t
       get_tsoffset(const bool start_at_zero = false) const
   {
      if ((start_time == AV_NOPTS_VALUE) || !accurate_seek)
         return AV_NOPTS_VALUE;

      int64_t tsoffset = start_time;
      if (!start_at_zero && ctx->start_time != AV_NOPTS_VALUE)
         tsoffset += ctx->start_time;

      return tsoffset;
   }

   void update_start_time();

   int64_t start_time; /* user-specified start time in AV_TIME_BASE or AV_NOPTS_VALUE */
   int64_t recording_time;
   
 protected:

   int64_t input_ts_offset;
   int64_t ts_offset;
   bool rate_emu;
   bool accurate_seek;
   int loop;              /* set number of times input stream should be looped */
   int64_t duration;      /* actual duration of the longest stream in a file
                             at the moment when looping happens */
   AVRational time_base;  /* time base of the duration */
   int thread_queue_size; /* maximum number of queued packets */
    bool eagain;           /* true if last read attempt returned EAGAIN */

    int eof_reached;      /* true if eof reached */
    int64_t last_ts;

   // int seek_timestamp; // used exclusively in seek() (for now)
   // int nb_streams; // now available as streams.size(): number of stream that ffmpeg is aware of; may be different from ctx.nb_streams if new streams appear during av_read_frame() */

 private:
   AVThreadMessageQueue *in_thread_queue;
   std::thread thread; /* thread reading from this file */
   bool non_blocking;   /* reading packets from the thread should not block */
   bool joined;         /* the thread has been joined */

   void add_input_streams(InputOptionsContext &o);

   void input_thread();
   int get_packet_once(AVPacket &pkt);
   int seek_to_start();
};

typedef std::vector<InputFile> InputFiles;
}
