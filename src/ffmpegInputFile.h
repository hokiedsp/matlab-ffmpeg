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

#ifdef AV_TIME_BASE_Q
#undef AV_TIME_BASE_Q
#define AV_TIME_BASE_Q AVRational({1,AV_TIME_BASE})
#endif

   int64_t get_tsoffset(const bool start_at_zero = false) const
   {
      if ((start_time == AV_NOPTS_VALUE) || !accurate_seek)
         return AV_NOPTS_VALUE;

      int64_t tsoffset = start_time;
      if (!start_at_zero && ctx->start_time != AV_NOPTS_VALUE)
         tsoffset += ctx->start_time;

      return tsoffset;
   }

   void update_start_time()
   {
      // Correcting starttime based on the enabled streams
      // FIXME this ideally should be done before the first use of starttime but we do not know which are the enabled streams at that point.
      //       so we instead do it here as part of discontinuity handling
      if (ts_offset == -ctx->start_time && (ctx->iformat->flags & AVFMT_TS_DISCONT))
      {
         int64_t new_start_time = INT64_MAX;
         for (unsigned int i = 0; i < ctx->nb_streams; i++)
         {
            AVStream *st = ctx->streams[i];
            if (st->discard == AVDISCARD_ALL || st->start_time == AV_NOPTS_VALUE)
               continue;
            new_start_time = FFMIN(new_start_time, av_rescale_q(st->start_time, st->time_base, AV_TIME_BASE_Q));
         }
         if (new_start_time > ctx->start_time)
         {
            //av_log(ctx, AV_LOG_VERBOSE, "Correcting start time by %" PRId64 "\n", new_start_time - is->start_time);
            ts_offset = -new_start_time;
         }
      }
   }

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
