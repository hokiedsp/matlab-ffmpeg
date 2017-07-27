/*
 * Copyright (c) 2000-2003 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * multimedia converter based on the FFmpeg libraries
 */

#include <string>

// #include "config.h"
// #include <ctype.h>
// #include <math.h>
// #include <stdlib.h>
// #include <errno.h>
// #include <limits.h>
// #include <stdint.h>

#if HAVE_IO_H
#include <io.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswresample/swresample.h"
#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/fifo.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avstring.h"
#include "libavutil/libm.h"
#include "libavutil/imgutils.h"
#include "libavutil/timestamp.h"
#include "libavutil/bprint.h"
#include "libavutil/time.h"
#include "libavutil/threadmessage.h"
#include "libavcodec/mathops.h"
#include "libavformat/os_support.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"

#if HAVE_SYS_RESOURCE_H
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#elif HAVE_GETPROCESSTIMES
#include <windows.h>
#endif
#if HAVE_GETPROCESSMEMORYINFO
#include <windows.h>
#include <psapi.h>
#endif
#if HAVE_SETCONSOLECTRLHANDLER
#include <windows.h>
#endif

#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#if HAVE_TERMIOS_H
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#elif HAVE_KBHIT
#include <conio.h>
#endif

#if HAVE_PTHREADS
#include <pthread.h>
#endif

#include <time.h>

#include "ffmpeg.h"
#include "cmdutils.h"

#include "libavutil/avassert.h"

const char program_name[] = "ffmpeg";
const int program_birth_year = 2000;


const char *const forced_keyframes_const_names[] = {
    "n",
    "n_forced",
    "prev_forced_n",
    "prev_forced_t",
    "t",
    NULL};

static void do_video_stats(OutputStream *ost, int frame_size);
static int64_t getutime(void);
static int64_t getmaxrss(void);

static int run_as_daemon = 0;
static int nb_frames_dup = 0;
static int nb_frames_drop = 0;

static int want_sdp = 1;

static int current_time;
AVIOContext *progress_avio = NULL;

static uint8_t *subtitle_out;

InputStreamRefs input_streams;
InputFiles input_files;

OutputStreamRefs output_streams;
OutputFiles output_files;

FilterGraphs filtergraphs;

std::string vstats_filename;
std::string sdp_filename;

#if HAVE_TERMIOS_H

/* init terminal so that we can grab keys */
static struct termios oldtty;
static int restore_tty;
#endif

static void free_input_threads(void);

static void term_exit_sigsafe(void)
{
#if HAVE_TERMIOS_H
   if (restore_tty)
      tcsetattr(0, TCSANOW, &oldtty);
#endif
}

void term_exit(void)
{
   av_log(NULL, AV_LOG_QUIET, "%s", "");
   term_exit_sigsafe();
}

static volatile int received_sigterm = 0;
static volatile int received_nb_signals = 0;
static volatile int transcode_init_done = 0;
static volatile int ffmpeg_exited = 0;
static int main_return_code = 0;

static void
sigterm_handler(int sig)
{
   received_sigterm = sig;
   received_nb_signals++;
   term_exit_sigsafe();
   if (received_nb_signals > 3)
   {
      write(2 /*STDERR_FILENO*/, "Received > 3 system signals, hard exiting\n",
            strlen("Received > 3 system signals, hard exiting\n"));

      exit(123);
   }
}

#if HAVE_SETCONSOLECTRLHANDLER
static BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
   av_log(NULL, AV_LOG_DEBUG, "\nReceived windows signal %ld\n", fdwCtrlType);

   switch (fdwCtrlType)
   {
   case CTRL_C_EVENT:
   case CTRL_BREAK_EVENT:
      sigterm_handler(SIGINT);
      return TRUE;

   case CTRL_CLOSE_EVENT:
   case CTRL_LOGOFF_EVENT:
   case CTRL_SHUTDOWN_EVENT:
      sigterm_handler(SIGTERM);
      /* Basically, with these 3 events, when we return from this method the
           process is hard terminated, so stall as long as we need to
           to try and let the main thread(s) clean up and gracefully terminate
           (we have at most 5 seconds, but should be done far before that). */
      while (!ffmpeg_exited)
      {
         Sleep(0);
      }
      return TRUE;

   default:
      av_log(NULL, AV_LOG_ERROR, "Received unknown windows signal %ld\n", fdwCtrlType);
      return FALSE;
   }
}
#endif

void term_init(void)
{
#if HAVE_TERMIOS_H
   if (!run_as_daemon && stdin_interaction)
   {
      struct termios tty;
      if (tcgetattr(0, &tty) == 0)
      {
         oldtty = tty;
         restore_tty = 1;

         tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
         tty.c_oflag |= OPOST;
         tty.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
         tty.c_cflag &= ~(CSIZE | PARENB);
         tty.c_cflag |= CS8;
         tty.c_cc[VMIN] = 1;
         tty.c_cc[VTIME] = 0;

         tcsetattr(0, TCSANOW, &tty);
      }
      signal(SIGQUIT, sigterm_handler); /* Quit (POSIX).  */
   }
#endif

   signal(SIGINT, sigterm_handler);  /* Interrupt (ANSI).    */
   signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */
#ifdef SIGXCPU
   signal(SIGXCPU, sigterm_handler);
#endif
#if HAVE_SETCONSOLECTRLHANDLER
   SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
#endif
}

/* read a key without blocking */
static int read_key(void)
{
   unsigned char ch;
#if HAVE_TERMIOS_H
   int n = 1;
   struct timeval tv;
   fd_set rfds;

   FD_ZERO(&rfds);
   FD_SET(0, &rfds);
   tv.tv_sec = 0;
   tv.tv_usec = 0;
   n = select(1, &rfds, NULL, NULL, &tv);
   if (n > 0)
   {
      n = read(0, &ch, 1);
      if (n == 1)
         return ch;

      return n;
   }
#elif HAVE_KBHIT
#if HAVE_PEEKNAMEDPIPE
   static int is_pipe;
   static HANDLE input_handle;
   DWORD dw, nchars;
   if (!input_handle)
   {
      input_handle = GetStdHandle(STD_INPUT_HANDLE);
      is_pipe = !GetConsoleMode(input_handle, &dw);
   }

   if (is_pipe)
   {
      /* When running under a GUI, you will end here. */
      if (!PeekNamedPipe(input_handle, NULL, 0, NULL, &nchars, NULL))
      {
         // input pipe may have been closed by the program that ran ffmpeg
         return -1;
      }
      //Read it
      if (nchars != 0)
      {
         read(0, &ch, 1);
         return ch;
      }
      else
      {
         return -1;
      }
   }
#endif
   if (kbhit())
      return (getch());
#endif
   return -1;
}

static void ffmpeg_cleanup(int ret)
{
   int i, j;

   if (do_benchmark)
   {
      int maxrss = getmaxrss() / 1024;
      av_log(NULL, AV_LOG_INFO, "bench: maxrss=%ikB\n", maxrss);
   }

   for (i = 0; i < nb_filtergraphs; i++)
   {
      FilterGraph *fg = filtergraphs[i];
      avfilter_graph_free(&fg->graph);
      for (j = 0; j < fg->nb_inputs; j++)
      {
         av_freep(&fg->inputs[j]->name);
         av_freep(&fg->inputs[j]);
      }
      av_freep(&fg->inputs);
      for (j = 0; j < fg->nb_outputs; j++)
      {
         av_freep(&fg->outputs[j]->name);
         av_freep(&fg->outputs[j]);
      }
      av_freep(&fg->outputs);
      av_freep(&fg->graph_desc);

      av_freep(&filtergraphs[i]);
   }
   av_freep(&filtergraphs);

   av_freep(&subtitle_out);

   /* close files */
   for (i = 0; i < nb_output_files; i++)
   {
      OutputFile *of = output_files[i];
      AVFormatContext *s;
      if (!of)
         continue;
      s = of->ctx;
      if (s && s->oformat && !(s->oformat->flags & AVFMT_NOFILE))
         avio_closep(&s->pb);
      avformat_free_context(s);
      av_dict_free(&of->opts);

      av_freep(&output_files[i]);
   }
   for (i = 0; i < nb_output_streams; i++)
   {
      OutputStream *ost = output_streams[i];

      if (!ost)
         continue;

      for (j = 0; j < ost->nb_bitstream_filters; j++)
         av_bsf_free(&ost->bsf_ctx[j]);
      av_freep(&ost->bsf_ctx);
      av_freep(&ost->bsf_extradata_updated);

      av_frame_free(&ost->filtered_frame);
      av_frame_free(&ost->last_frame);
      av_dict_free(&ost->encoder_opts);

      av_parser_close(ost->parser);
      avcodec_free_context(&ost->parser_avctx);

      av_freep(&ost->forced_keyframes);
      av_expr_free(ost->forced_keyframes_pexpr);
      av_freep(&ost->avfilter);
      av_freep(&ost->logfile_prefix);

      av_dict_free(&ost->sws_dict);

      avcodec_free_context(&ost->enc_ctx);
      avcodec_parameters_free(&ost->ref_par);

      while (ost->muxing_queue && av_fifo_size(ost->muxing_queue))
      {
         AVPacket pkt;
         av_fifo_generic_read(ost->muxing_queue, &pkt, sizeof(pkt), NULL);
         av_packet_unref(&pkt);
      }
      av_fifo_freep(&ost->muxing_queue);

      av_freep(&output_streams[i]);
   }

   free_input_threads();

   if (vstats_file)
   {
      if (fclose(vstats_file))
         av_log(NULL, AV_LOG_ERROR,
                "Error closing vstats file, loss of information possible: %s\n",
                av_err2str(AVERROR(errno)));
   }
   av_freep(&vstats_filename);

   av_freep(&input_streams);
   av_freep(&input_files);
   av_freep(&output_streams);
   av_freep(&output_files);

   uninit_opts();

   avformat_network_deinit();

   if (received_sigterm)
   {
      av_log(NULL, AV_LOG_INFO, "Exiting normally, received signal %d.\n",
             (int)received_sigterm);
   }
   else if (ret && transcode_init_done)
   {
      av_log(NULL, AV_LOG_INFO, "Conversion failed!\n");
   }
   term_exit();
   ffmpeg_exited = 1;
}

static void abort_codec_experimental(AVCodec *c, int encoder)
{
   exit_program(1);
}

static void close_all_output_streams(OutputStream *ost, OSTFinished this_stream, OSTFinished others)
{
   int i;
   for (i = 0; i < nb_output_streams; i++)
   {
      OutputStream *ost2 = output_streams[i];
      ost2->finished |= ost == ost2 ? this_stream : others;
   }
}

static void write_packet(OutputFile *of, AVPacket *pkt, OutputStream *ost)
{
   AVFormatContext *s = of->ctx;
   AVStream *st = ost->st;
   int ret;

   if (!of->header_written)
   {
      AVPacket tmp_pkt;
      /* the muxer is not initialized yet, buffer the packet */
      if (!av_fifo_space(ost->muxing_queue))
      {
         int new_size = FFMIN(2 * av_fifo_size(ost->muxing_queue),
                              ost->max_muxing_queue_size);
         if (new_size <= av_fifo_size(ost->muxing_queue))
         {
            av_log(NULL, AV_LOG_ERROR,
                   "Too many packets buffered for output stream %d:%d.\n",
                   ost->file_index, ost->st->index);
            exit_program(1);
         }
         ret = av_fifo_realloc2(ost->muxing_queue, new_size);
         if (ret < 0)
            exit_program(1);
      }
      av_packet_move_ref(&tmp_pkt, pkt);
      av_fifo_generic_write(ost->muxing_queue, &tmp_pkt, sizeof(tmp_pkt), NULL);
      return;
   }

   if ((st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_sync_method == VSYNC_DROP) ||
       (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_sync_method < 0))
      pkt->pts = pkt->dts = AV_NOPTS_VALUE;

   /*
     * Audio encoders may split the packets --  #frames in != #packets out.
     * But there is no reordering, so we can limit the number of output packets
     * by simply dropping them here.
     * Counting encoded video frames needs to be done separately because of
     * reordering, see do_video_out()
     */
   if (!(st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && ost->encoding_needed))
   {
      if (ost->frame_number >= ost->max_frames)
      {
         av_packet_unref(pkt);
         return;
      }
      ost->frame_number++;
   }
   if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
   {
      int i;
      uint8_t *sd = av_packet_get_side_data(pkt, AV_PKT_DATA_QUALITY_STATS,
                                            NULL);
      ost->quality = sd ? AV_RL32(sd) : -1;
      ost->pict_type = sd ? sd[4] : AV_PICTURE_TYPE_NONE;

      for (i = 0; i < FF_ARRAY_ELEMS(ost->error); i++)
      {
         if (sd && i < sd[5])
            ost->error[i] = AV_RL64(sd + 8 + 8 * i);
         else
            ost->error[i] = -1;
      }

      if (ost->frame_rate.num && ost->is_cfr)
      {
         if (pkt->duration > 0)
            av_log(NULL, AV_LOG_WARNING, "Overriding packet duration by frame rate, this should not happen\n");
         pkt->duration = av_rescale_q(1, av_inv_q(ost->frame_rate),
                                      ost->st->time_base);
      }
   }

   if (!(s->oformat->flags & AVFMT_NOTIMESTAMPS))
   {
      if (pkt->dts != AV_NOPTS_VALUE &&
          pkt->pts != AV_NOPTS_VALUE &&
          pkt->dts > pkt->pts)
      {
         av_log(s, AV_LOG_WARNING, "Invalid DTS: %" PRId64 " PTS: %" PRId64 " in output stream %d:%d, replacing by guess\n",
                pkt->dts, pkt->pts,
                ost->file_index, ost->st->index);
         pkt->pts =
             pkt->dts = pkt->pts + pkt->dts + ost->last_mux_dts + 1 - FFMIN3(pkt->pts, pkt->dts, ost->last_mux_dts + 1) - FFMAX3(pkt->pts, pkt->dts, ost->last_mux_dts + 1);
      }
      if ((st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO || st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) &&
          pkt->dts != AV_NOPTS_VALUE &&
          !(st->codecpar->codec_id == AV_CODEC_ID_VP9 && ost->stream_copy) &&
          ost->last_mux_dts != AV_NOPTS_VALUE)
      {
         int64_t max = ost->last_mux_dts + !(s->oformat->flags & AVFMT_TS_NONSTRICT);
         if (pkt->dts < max)
         {
            int loglevel = max - pkt->dts > 2 || st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ? AV_LOG_WARNING : AV_LOG_DEBUG;
            av_log(s, loglevel, "Non-monotonous DTS in output stream "
                                "%d:%d; previous: %" PRId64 ", current: %" PRId64 "; ",
                   ost->file_index, ost->st->index, ost->last_mux_dts, pkt->dts);
            if (exit_on_error)
            {
               av_log(NULL, AV_LOG_FATAL, "aborting.\n");
               exit_program(1);
            }
            av_log(s, loglevel, "changing to %" PRId64 ". This may result "
                                "in incorrect timestamps in the output file.\n",
                   max);
            if (pkt->pts >= pkt->dts)
               pkt->pts = FFMAX(pkt->pts, max);
            pkt->dts = max;
         }
      }
   }
   ost->last_mux_dts = pkt->dts;

   ost->data_size += pkt->size;
   ost->packets_written++;

   pkt->stream_index = ost->index;

   if (debug_ts)
   {
      av_log(NULL, AV_LOG_INFO, "muxer <- type:%s "
                                "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s size:%d\n",
             av_get_media_type_string(ost->enc_ctx->codec_type),
             av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &ost->st->time_base),
             av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &ost->st->time_base),
             pkt->size);
   }

   ret = av_interleaved_write_frame(s, pkt);
   if (ret < 0)
   {
      print_error("av_interleaved_write_frame()", ret);
      main_return_code = 1;
      close_all_output_streams(ost, MUXER_FINISHED | ENCODER_FINISHED, ENCODER_FINISHED);
   }
   av_packet_unref(pkt);
}

static int check_recording_time(OutputStream *ost)
{
   OutputFile *of = output_files[ost->file_index];

   if (of->recording_time != INT64_MAX &&
       av_compare_ts(ost->sync_opts - ost->first_pts, ost->enc_ctx->time_base, of->recording_time,
                     AV_TIME_BASE_Q) >= 0)
   {
      close_output_stream(ost);
      return 0;
   }
   return 1;
}


static double psnr(double d)
{
   return -10.0 * log10(d);
}

static void do_video_stats(OutputStream *ost, int frame_size)
{
   AVCodecContext *enc;
   int frame_number;
   double ti1, bitrate, avg_bitrate;

   /* this is executed just the first time do_video_stats is called */
   if (!vstats_file)
   {
      vstats_file = fopen(vstats_filename, "w");
      if (!vstats_file)
      {
         perror("fopen");
         exit_program(1);
      }
   }

   enc = ost->enc_ctx;
   if (enc->codec_type == AVMEDIA_TYPE_VIDEO)
   {
      frame_number = ost->st->nb_frames;
      fprintf(vstats_file, "frame= %5d q= %2.1f ", frame_number,
              ost->quality / (float)FF_QP2LAMBDA);

      if (ost->error[0] >= 0 && (enc->flags & AV_CODEC_FLAG_PSNR))
         fprintf(vstats_file, "PSNR= %6.2f ", psnr(ost->error[0] / (enc->width * enc->height * 255.0 * 255.0)));

      fprintf(vstats_file, "f_size= %6d ", frame_size);
      /* compute pts value */
      ti1 = av_stream_get_end_pts(ost->st) * av_q2d(ost->st->time_base);
      if (ti1 < 0.01)
         ti1 = 0.01;

      bitrate = (frame_size * 8) / av_q2d(enc->time_base) / 1000.0;
      avg_bitrate = (double)(ost->data_size * 8) / ti1 / 1000.0;
      fprintf(vstats_file, "s_size= %8.0fkB time= %0.3f br= %7.1fkbits/s avg_br= %7.1fkbits/s ",
              (double)ost->data_size / 1024, ti1, bitrate, avg_bitrate);
      fprintf(vstats_file, "type= %c\n", av_get_picture_type_char(ost->pict_type));
   }
}

static void print_final_stats(int64_t total_size)
{
   uint64_t video_size = 0, audio_size = 0, extra_size = 0, other_size = 0;
   uint64_t subtitle_size = 0;
   uint64_t data_size = 0;
   float percent = -1.0;
   int i, j;
   int pass1_used = 1;

   for (i = 0; i < nb_output_streams; i++)
   {
      OutputStream *ost = output_streams[i];
      switch (ost->enc_ctx->codec_type)
      {
      case AVMEDIA_TYPE_VIDEO:
         video_size += ost->data_size;
         break;
      case AVMEDIA_TYPE_AUDIO:
         audio_size += ost->data_size;
         break;
      case AVMEDIA_TYPE_SUBTITLE:
         subtitle_size += ost->data_size;
         break;
      default:
         other_size += ost->data_size;
         break;
      }
      extra_size += ost->enc_ctx->extradata_size;
      data_size += ost->data_size;
      if ((ost->enc_ctx->flags & (AV_CODEC_FLAG_PASS1 | CODEC_FLAG_PASS2)) != AV_CODEC_FLAG_PASS1)
         pass1_used = 0;
   }

   if (data_size && total_size > 0 && total_size >= data_size)
      percent = 100.0 * (total_size - data_size) / data_size;

   av_log(NULL, AV_LOG_INFO, "video:%1.0fkB audio:%1.0fkB subtitle:%1.0fkB other streams:%1.0fkB global headers:%1.0fkB muxing overhead: ",
          video_size / 1024.0,
          audio_size / 1024.0,
          subtitle_size / 1024.0,
          other_size / 1024.0,
          extra_size / 1024.0);
   if (percent >= 0.0)
      av_log(NULL, AV_LOG_INFO, "%f%%", percent);
   else
      av_log(NULL, AV_LOG_INFO, "unknown");
   av_log(NULL, AV_LOG_INFO, "\n");

   /* print verbose per-stream stats */
   for (i = 0; i < nb_input_files; i++)
   {
      InputFile *f = input_files[i];
      uint64_t total_packets = 0, total_size = 0;

      av_log(NULL, AV_LOG_VERBOSE, "Input file #%d (%s):\n",
             i, f->ctx->filename);

      for (j = 0; j < f->nb_streams; j++)
      {
         InputStream *ist = input_streams[f->ist_index + j];
         enum AVMediaType type = ist->dec_ctx->codec_type;

         total_size += ist->data_size;
         total_packets += ist->nb_packets;

         av_log(NULL, AV_LOG_VERBOSE, "  Input stream #%d:%d (%s): ",
                i, j, media_type_string(type));
         av_log(NULL, AV_LOG_VERBOSE, "%" PRIu64 " packets read (%" PRIu64 " bytes); ",
                ist->nb_packets, ist->data_size);

         if (ist->decoding_needed)
         {
            av_log(NULL, AV_LOG_VERBOSE, "%" PRIu64 " frames decoded",
                   ist->frames_decoded);
            if (type == AVMEDIA_TYPE_AUDIO)
               av_log(NULL, AV_LOG_VERBOSE, " (%" PRIu64 " samples)", ist->samples_decoded);
            av_log(NULL, AV_LOG_VERBOSE, "; ");
         }

         av_log(NULL, AV_LOG_VERBOSE, "\n");
      }

      av_log(NULL, AV_LOG_VERBOSE, "  Total: %" PRIu64 " packets (%" PRIu64 " bytes) demuxed\n",
             total_packets, total_size);
   }

   for (i = 0; i < nb_output_files; i++)
   {
      OutputFile *of = output_files[i];
      uint64_t total_packets = 0, total_size = 0;

      av_log(NULL, AV_LOG_VERBOSE, "Output file #%d (%s):\n",
             i, of->ctx->filename);

      for (j = 0; j < of->ctx->nb_streams; j++)
      {
         OutputStream *ost = output_streams[of->ost_index + j];
         enum AVMediaType type = ost->enc_ctx->codec_type;

         total_size += ost->data_size;
         total_packets += ost->packets_written;

         av_log(NULL, AV_LOG_VERBOSE, "  Output stream #%d:%d (%s): ",
                i, j, media_type_string(type));
         if (ost->encoding_needed)
         {
            av_log(NULL, AV_LOG_VERBOSE, "%" PRIu64 " frames encoded",
                   ost->frames_encoded);
            if (type == AVMEDIA_TYPE_AUDIO)
               av_log(NULL, AV_LOG_VERBOSE, " (%" PRIu64 " samples)", ost->samples_encoded);
            av_log(NULL, AV_LOG_VERBOSE, "; ");
         }

         av_log(NULL, AV_LOG_VERBOSE, "%" PRIu64 " packets muxed (%" PRIu64 " bytes); ",
                ost->packets_written, ost->data_size);

         av_log(NULL, AV_LOG_VERBOSE, "\n");
      }

      av_log(NULL, AV_LOG_VERBOSE, "  Total: %" PRIu64 " packets (%" PRIu64 " bytes) muxed\n",
             total_packets, total_size);
   }
   if (video_size + data_size + audio_size + subtitle_size + extra_size == 0)
   {
      av_log(NULL, AV_LOG_WARNING, "Output file is empty, nothing was encoded ");
      if (pass1_used)
      {
         av_log(NULL, AV_LOG_WARNING, "\n");
      }
      else
      {
         av_log(NULL, AV_LOG_WARNING, "(check -ss / -t / -frames parameters if used)\n");
      }
   }
}

static void print_report(int is_last_report, int64_t timer_start, int64_t cur_time)
{
   char buf[1024];
   AVBPrint buf_script;
   OutputStream *ost;
   AVFormatContext *oc;
   int64_t total_size;
   AVCodecContext *enc;
   int frame_number, vid, i;
   double bitrate;
   double speed;
   int64_t pts = INT64_MIN + 1;
   static int64_t last_time = -1;
   static int qp_histogram[52];
   int hours, mins, secs, us;
   int ret;
   float t;

   if (!print_stats && !is_last_report && !progress_avio)
      return;

   if (!is_last_report)
   {
      if (last_time == -1)
      {
         last_time = cur_time;
         return;
      }
      if ((cur_time - last_time) < 500000)
         return;
      last_time = cur_time;
   }

   t = (cur_time - timer_start) / 1000000.0;

   oc = output_files[0]->ctx;

   total_size = avio_size(oc->pb);
   if (total_size <= 0) // FIXME improve avio_size() so it works with non seekable output too
      total_size = avio_tell(oc->pb);

   buf[0] = '\0';
   vid = 0;
   av_bprint_init(&buf_script, 0, 1);
   for (i = 0; i < nb_output_streams; i++)
   {
      float q = -1;
      ost = output_streams[i];
      enc = ost->enc_ctx;
      if (!ost->stream_copy)
         q = ost->quality / (float)FF_QP2LAMBDA;

      if (vid && enc->codec_type == AVMEDIA_TYPE_VIDEO)
      {
         snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "q=%2.1f ", q);
         av_bprintf(&buf_script, "stream_%d_%d_q=%.1f\n",
                    ost->file_index, ost->index, q);
      }
      if (!vid && enc->codec_type == AVMEDIA_TYPE_VIDEO)
      {
         float fps;

         frame_number = ost->frame_number;
         fps = t > 1 ? frame_number / t : 0;
         snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "frame=%5d fps=%3.*f q=%3.1f ",
                  frame_number, fps < 9.95, fps, q);
         av_bprintf(&buf_script, "frame=%d\n", frame_number);
         av_bprintf(&buf_script, "fps=%.1f\n", fps);
         av_bprintf(&buf_script, "stream_%d_%d_q=%.1f\n",
                    ost->file_index, ost->index, q);
         if (is_last_report)
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "L");
         if (qp_hist)
         {
            int j;
            int qp = lrintf(q);
            if (qp >= 0 && qp < FF_ARRAY_ELEMS(qp_histogram))
               qp_histogram[qp]++;
            for (j = 0; j < 32; j++)
               snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%X", av_log2(qp_histogram[j] + 1));
         }

         if ((enc->flags & AV_CODEC_FLAG_PSNR) && (ost->pict_type != AV_PICTURE_TYPE_NONE || is_last_report))
         {
            int j;
            double error, error_sum = 0;
            double scale, scale_sum = 0;
            double p;
            char type[3] = {'Y', 'U', 'V'};
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "PSNR=");
            for (j = 0; j < 3; j++)
            {
               if (is_last_report)
               {
                  error = enc->error[j];
                  scale = enc->width * enc->height * 255.0 * 255.0 * frame_number;
               }
               else
               {
                  error = ost->error[j];
                  scale = enc->width * enc->height * 255.0 * 255.0;
               }
               if (j)
                  scale /= 4;
               error_sum += error;
               scale_sum += scale;
               p = psnr(error / scale);
               snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%c:%2.2f ", type[j], p);
               av_bprintf(&buf_script, "stream_%d_%d_psnr_%c=%2.2f\n",
                          ost->file_index, ost->index, type[j] | 32, p);
            }
            p = psnr(error_sum / scale_sum);
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "*:%2.2f ", psnr(error_sum / scale_sum));
            av_bprintf(&buf_script, "stream_%d_%d_psnr_all=%2.2f\n",
                       ost->file_index, ost->index, p);
         }
         vid = 1;
      }
      /* compute min output value */
      if (av_stream_get_end_pts(ost->st) != AV_NOPTS_VALUE)
         pts = FFMAX(pts, av_rescale_q(av_stream_get_end_pts(ost->st),
                                       ost->st->time_base, AV_TIME_BASE_Q));
      if (is_last_report)
         nb_frames_drop += ost->last_dropped;
   }

   secs = FFABS(pts) / AV_TIME_BASE;
   us = FFABS(pts) % AV_TIME_BASE;
   mins = secs / 60;
   secs %= 60;
   hours = mins / 60;
   mins %= 60;

   bitrate = pts && total_size >= 0 ? total_size * 8 / (pts / 1000.0) : -1;
   speed = t != 0.0 ? (double)pts / AV_TIME_BASE / t : -1;

   if (total_size < 0)
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
               "size=N/A time=");
   else
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
               "size=%8.0fkB time=", total_size / 1024.0);
   if (pts < 0)
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "-");
   snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
            "%02d:%02d:%02d.%02d ", hours, mins, secs,
            (100 * us) / AV_TIME_BASE);

   if (bitrate < 0)
   {
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "bitrate=N/A");
      av_bprintf(&buf_script, "bitrate=N/A\n");
   }
   else
   {
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "bitrate=%6.1fkbits/s", bitrate);
      av_bprintf(&buf_script, "bitrate=%6.1fkbits/s\n", bitrate);
   }

   if (total_size < 0)
      av_bprintf(&buf_script, "total_size=N/A\n");
   else
      av_bprintf(&buf_script, "total_size=%" PRId64 "\n", total_size);
   av_bprintf(&buf_script, "out_time_ms=%" PRId64 "\n", pts);
   av_bprintf(&buf_script, "out_time=%02d:%02d:%02d.%06d\n",
              hours, mins, secs, us);

   if (nb_frames_dup || nb_frames_drop)
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " dup=%d drop=%d",
               nb_frames_dup, nb_frames_drop);
   av_bprintf(&buf_script, "dup_frames=%d\n", nb_frames_dup);
   av_bprintf(&buf_script, "drop_frames=%d\n", nb_frames_drop);

   if (speed < 0)
   {
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " speed=N/A");
      av_bprintf(&buf_script, "speed=N/A\n");
   }
   else
   {
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " speed=%4.3gx", speed);
      av_bprintf(&buf_script, "speed=%4.3gx\n", speed);
   }

   if (print_stats || is_last_report)
   {
      const char end = is_last_report ? '\n' : '\r';
      if (print_stats == 1 && AV_LOG_INFO > av_log_get_level())
      {
         fprintf(stderr, "%s    %c", buf, end);
      }
      else
         av_log(NULL, AV_LOG_INFO, "%s    %c", buf, end);

      fflush(stderr);
   }

   if (progress_avio)
   {
      av_bprintf(&buf_script, "progress=%s\n",
                 is_last_report ? "end" : "continue");
      avio_write(progress_avio, buf_script.str,
                 FFMIN(buf_script.len, buf_script.size - 1));
      avio_flush(progress_avio);
      av_bprint_finalize(&buf_script, NULL);
      if (is_last_report)
      {
         if ((ret = avio_closep(&progress_avio)) < 0)
            av_log(NULL, AV_LOG_ERROR,
                   "Error closing progress log, loss of information possible: %s\n", av_err2str(ret));
      }
   }

   if (is_last_report)
      print_final_stats(total_size);
}

static void print_sdp(void)
{
   char sdp[16384];
   int i;
   int j;
   AVIOContext *sdp_pb;
   AVFormatContext **avc;

   for (i = 0; i < nb_output_files; i++)
   {
      if (!output_files[i]->header_written)
         return;
   }

   avc = av_malloc_array(nb_output_files, sizeof(*avc));
   if (!avc)
      exit_program(1);
   for (i = 0, j = 0; i < nb_output_files; i++)
   {
      if (!strcmp(output_files[i]->ctx->oformat->name, "rtp"))
      {
         avc[j] = output_files[i]->ctx;
         j++;
      }
   }

   if (!j)
      goto fail;

   av_sdp_create(avc, j, sdp, sizeof(sdp));

   if (!sdp_filename)
   {
      printf("SDP:\n%s\n", sdp);
      fflush(stdout);
   }
   else
   {
      if (avio_open2(&sdp_pb, sdp_filename, AVIO_FLAG_WRITE, &int_cb, NULL) < 0)
      {
         av_log(NULL, AV_LOG_ERROR, "Failed to open sdp file '%s'\n", sdp_filename);
      }
      else
      {
         avio_printf(sdp_pb, "SDP:\n%s", sdp);
         avio_closep(&sdp_pb);
         av_freep(&sdp_filename);
      }
   }

fail:
   av_freep(&avc);
}

static int compare_int64(const void *a, const void *b)
{
   return FFDIFFSIGN(*(const int64_t *)a, *(const int64_t *)b);
}


static int init_output_bsfs(OutputStream *ost)
{
   AVBSFContext *ctx;
   int i, ret;

   if (!ost->nb_bitstream_filters)
      return 0;

   for (i = 0; i < ost->nb_bitstream_filters; i++)
   {
      ctx = ost->bsf_ctx[i];

      ret = avcodec_parameters_copy(ctx->par_in,
                                    i ? ost->bsf_ctx[i - 1]->par_out : ost->st->codecpar);
      if (ret < 0)
         return ret;

      ctx->time_base_in = i ? ost->bsf_ctx[i - 1]->time_base_out : ost->st->time_base;

      ret = av_bsf_init(ctx);
      if (ret < 0)
      {
         av_log(NULL, AV_LOG_ERROR, "Error initializing bitstream filter: %s\n",
                ost->bsf_ctx[i]->filter->name);
         return ret;
      }
   }

   ctx = ost->bsf_ctx[ost->nb_bitstream_filters - 1];
   ret = avcodec_parameters_copy(ost->st->codecpar, ctx->par_out);
   if (ret < 0)
      return ret;

   ost->st->time_base = ctx->time_base_out;

   return 0;
}

static int init_output_stream_streamcopy(OutputStream *ost)
{
   OutputFile *of = output_files[ost->file_index];
   InputStream *ist = get_input_stream(ost);
   AVCodecParameters *par_dst = ost->st->codecpar;
   AVCodecParameters *par_src = ost->ref_par;
   AVRational sar;
   int i, ret;
   uint64_t extra_size;

   av_assert0(ist && !ost->filter);

   avcodec_parameters_to_context(ost->enc_ctx, ist->st->codecpar);
   ret = av_opt_set_dict(ost->enc_ctx, &ost->encoder_opts);
   if (ret < 0)
   {
      av_log(NULL, AV_LOG_FATAL,
             "Error setting up codec context options.\n");
      return ret;
   }
   avcodec_parameters_from_context(par_src, ost->enc_ctx);

   extra_size = (uint64_t)par_src->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE;

   if (extra_size > INT_MAX)
   {
      return AVERROR(EINVAL);
   }

   /* if stream_copy is selected, no need to decode or encode */
   par_dst->codec_id = par_src->codec_id;
   par_dst->codec_type = par_src->codec_type;

   if (!par_dst->codec_tag)
   {
      unsigned int codec_tag;
      if (!of->ctx->oformat->codec_tag ||
          av_codec_get_id(of->ctx->oformat->codec_tag, par_src->codec_tag) == par_dst->codec_id ||
          !av_codec_get_tag2(of->ctx->oformat->codec_tag, par_src->codec_id, &codec_tag))
         par_dst->codec_tag = par_src->codec_tag;
   }

   par_dst->bit_rate = par_src->bit_rate;
   par_dst->field_order = par_src->field_order;
   par_dst->chroma_location = par_src->chroma_location;

   if (par_src->extradata_size)
   {
      par_dst->extradata = av_mallocz(extra_size);
      if (!par_dst->extradata)
      {
         return AVERROR(ENOMEM);
      }
      memcpy(par_dst->extradata, par_src->extradata, par_src->extradata_size);
      par_dst->extradata_size = par_src->extradata_size;
   }
   par_dst->bits_per_coded_sample = par_src->bits_per_coded_sample;
   par_dst->bits_per_raw_sample = par_src->bits_per_raw_sample;

   if (!ost->frame_rate.num)
      ost->frame_rate = ist->framerate;
   ost->st->avg_frame_rate = ost->frame_rate;

   ret = avformat_transfer_internal_stream_timing_info(of->ctx->oformat, ost->st, ist->st, copy_tb);
   if (ret < 0)
      return ret;

   // copy timebase while removing common factors
   ost->st->time_base = av_add_q(av_stream_get_codec_timebase(ost->st), (AVRational){0, 1});

   if (ist->st->nb_side_data)
   {
      ost->st->side_data = av_realloc_array(NULL, ist->st->nb_side_data,
                                            sizeof(*ist->st->side_data));
      if (!ost->st->side_data)
         return AVERROR(ENOMEM);

      ost->st->nb_side_data = 0;
      for (i = 0; i < ist->st->nb_side_data; i++)
      {
         const AVPacketSideData *sd_src = &ist->st->side_data[i];
         AVPacketSideData *sd_dst = &ost->st->side_data[ost->st->nb_side_data];

         if (ost->rotate_overridden && sd_src->type == AV_PKT_DATA_DISPLAYMATRIX)
            continue;

         sd_dst->data = av_malloc(sd_src->size);
         if (!sd_dst->data)
            return AVERROR(ENOMEM);
         memcpy(sd_dst->data, sd_src->data, sd_src->size);
         sd_dst->size = sd_src->size;
         sd_dst->type = sd_src->type;
         ost->st->nb_side_data++;
      }
   }

   ost->parser = av_parser_init(par_dst->codec_id);
   ost->parser_avctx = avcodec_alloc_context3(NULL);
   if (!ost->parser_avctx)
      return AVERROR(ENOMEM);

   switch (par_dst->codec_type)
   {
   case AVMEDIA_TYPE_AUDIO:
      if (audio_volume != 256)
      {
         av_log(NULL, AV_LOG_FATAL, "-acodec copy and -vol are incompatible (frames are not decoded)\n");
         exit_program(1);
      }
      par_dst->channel_layout = par_src->channel_layout;
      par_dst->sample_rate = par_src->sample_rate;
      par_dst->channels = par_src->channels;
      par_dst->frame_size = par_src->frame_size;
      par_dst->block_align = par_src->block_align;
      par_dst->initial_padding = par_src->initial_padding;
      par_dst->trailing_padding = par_src->trailing_padding;
      par_dst->profile = par_src->profile;
      if ((par_dst->block_align == 1 || par_dst->block_align == 1152 || par_dst->block_align == 576) && par_dst->codec_id == AV_CODEC_ID_MP3)
         par_dst->block_align = 0;
      if (par_dst->codec_id == AV_CODEC_ID_AC3)
         par_dst->block_align = 0;
      break;
   case AVMEDIA_TYPE_VIDEO:
      par_dst->format = par_src->format;
      par_dst->color_space = par_src->color_space;
      par_dst->color_range = par_src->color_range;
      par_dst->color_primaries = par_src->color_primaries;
      par_dst->color_trc = par_src->color_trc;
      par_dst->width = par_src->width;
      par_dst->height = par_src->height;
      par_dst->video_delay = par_src->video_delay;
      par_dst->profile = par_src->profile;
      if (ost->frame_aspect_ratio.num)
      { // overridden by the -aspect cli option
         sar =
             av_mul_q(ost->frame_aspect_ratio,
                      (AVRational){par_dst->height, par_dst->width});
         av_log(NULL, AV_LOG_WARNING, "Overriding aspect ratio "
                                      "with stream copy may produce invalid files\n");
      }
      else if (ist->st->sample_aspect_ratio.num)
         sar = ist->st->sample_aspect_ratio;
      else
         sar = par_src->sample_aspect_ratio;
      ost->st->sample_aspect_ratio = par_dst->sample_aspect_ratio = sar;
      ost->st->avg_frame_rate = ist->st->avg_frame_rate;
      ost->st->r_frame_rate = ist->st->r_frame_rate;
      break;
   case AVMEDIA_TYPE_SUBTITLE:
      par_dst->width = par_src->width;
      par_dst->height = par_src->height;
      break;
   case AVMEDIA_TYPE_UNKNOWN:
   case AVMEDIA_TYPE_DATA:
   case AVMEDIA_TYPE_ATTACHMENT:
      break;
   default:
      abort();
   }

   return 0;
}




static void set_tty_echo(int on)
{
#if HAVE_TERMIOS_H
   struct termios tty;
   if (tcgetattr(0, &tty) == 0)
   {
      if (on)
         tty.c_lflag |= ECHO;
      else
         tty.c_lflag &= ~ECHO;
      tcsetattr(0, TCSANOW, &tty);
   }
#endif
}

static int check_keyboard_interaction(int64_t cur_time)
{
   int i, ret, key;
   static int64_t last_time;
   if (received_nb_signals)
      return AVERROR_EXIT;
   /* read_key() returns 0 on EOF */
   if (cur_time - last_time >= 100000 && !run_as_daemon)
   {
      key = read_key();
      last_time = cur_time;
   }
   else
      key = -1;
   if (key == 'q')
      return AVERROR_EXIT;
   if (key == '+')
      av_log_set_level(av_log_get_level() + 10);
   if (key == '-')
      av_log_set_level(av_log_get_level() - 10);
   if (key == 's')
      qp_hist ^= 1;
   if (key == 'h')
   {
      if (do_hex_dump)
      {
         do_hex_dump = do_pkt_dump = 0;
      }
      else if (do_pkt_dump)
      {
         do_hex_dump = 1;
      }
      else
         do_pkt_dump = 1;
      av_log_set_level(AV_LOG_DEBUG);
   }
   if (key == 'c' || key == 'C')
   {
      char buf[4096], target[64], command[256], arg[256] = {0};
      double time;
      int k, n = 0;
      fprintf(stderr, "\nEnter command: <target>|all <time>|-1 <command>[ <argument>]\n");
      i = 0;
      set_tty_echo(1);
      while ((k = read_key()) != '\n' && k != '\r' && i < sizeof(buf) - 1)
         if (k > 0)
            buf[i++] = k;
      buf[i] = 0;
      set_tty_echo(0);
      fprintf(stderr, "\n");
      if (k > 0 &&
          (n = sscanf(buf, "%63[^ ] %lf %255[^ ] %255[^\n]", target, &time, command, arg)) >= 3)
      {
         av_log(NULL, AV_LOG_DEBUG, "Processing command target:%s time:%f command:%s arg:%s",
                target, time, command, arg);
         for (i = 0; i < nb_filtergraphs; i++)
         {
            FilterGraph *fg = filtergraphs[i];
            if (fg->graph)
            {
               if (time < 0)
               {
                  ret = avfilter_graph_send_command(fg->graph, target, command, arg, buf, sizeof(buf),
                                                    key == 'c' ? AVFILTER_CMD_FLAG_ONE : 0);
                  fprintf(stderr, "Command reply for stream %d: ret:%d res:\n%s", i, ret, buf);
               }
               else if (key == 'c')
               {
                  fprintf(stderr, "Queuing commands only on filters supporting the specific command is unsupported\n");
                  ret = AVERROR_PATCHWELCOME;
               }
               else
               {
                  ret = avfilter_graph_queue_command(fg->graph, target, command, arg, 0, time);
                  if (ret < 0)
                     fprintf(stderr, "Queuing command failed with error %s\n", av_err2str(ret));
               }
            }
         }
      }
      else
      {
         av_log(NULL, AV_LOG_ERROR,
                "Parse error, at least 3 arguments were expected, "
                "only %d given in string '%s'\n",
                n, buf);
      }
   }
   if (key == 'd' || key == 'D')
   {
      int debug = 0;
      if (key == 'D')
      {
         debug = input_streams[0]->st->codec->debug << 1;
         if (!debug)
            debug = 1;
         while (debug & (FF_DEBUG_DCT_COEFF | FF_DEBUG_VIS_QP | FF_DEBUG_VIS_MB_TYPE)) //unsupported, would just crash
            debug += debug;
      }
      else
      {
         char buf[32];
         int k = 0;
         i = 0;
         set_tty_echo(1);
         while ((k = read_key()) != '\n' && k != '\r' && i < sizeof(buf) - 1)
            if (k > 0)
               buf[i++] = k;
         buf[i] = 0;
         set_tty_echo(0);
         fprintf(stderr, "\n");
         if (k <= 0 || sscanf(buf, "%d", &debug) != 1)
            fprintf(stderr, "error parsing debug value\n");
      }
      for (i = 0; i < nb_input_streams; i++)
      {
         input_streams[i]->st->codec->debug = debug;
      }
      for (i = 0; i < nb_output_streams; i++)
      {
         OutputStream *ost = output_streams[i];
         ost->enc_ctx->debug = debug;
      }
      if (debug)
         av_log_set_level(AV_LOG_DEBUG);
      fprintf(stderr, "debug=%d\n", debug);
   }
   if (key == '?')
   {
      fprintf(stderr, "key    function\n"
                      "?      show this help\n"
                      "+      increase verbosity\n"
                      "-      decrease verbosity\n"
                      "c      Send command to first matching filter supporting it\n"
                      "C      Send/Queue command to all matching filters\n"
                      "D      cycle through available debug modes\n"
                      "h      dump packets/hex press to cycle through the 3 states\n"
                      "q      quit\n"
                      "s      Show QP histogram\n");
   }
   return 0;
}

static int got_eagain(void)
{
   int i;
   for (i = 0; i < nb_output_streams; i++)
      if (output_streams[i]->unavailable)
         return 1;
   return 0;
}

static int64_t getutime(void)
{
#if HAVE_GETRUSAGE
   struct rusage rusage;

   getrusage(RUSAGE_SELF, &rusage);
   return (rusage.ru_utime.tv_sec * 1000000LL) + rusage.ru_utime.tv_usec;
#elif HAVE_GETPROCESSTIMES
   HANDLE proc;
   FILETIME c, e, k, u;
   proc = GetCurrentProcess();
   GetProcessTimes(proc, &c, &e, &k, &u);
   return ((int64_t)u.dwHighDateTime << 32 | u.dwLowDateTime) / 10;
#else
   return av_gettime_relative();
#endif
}

static int64_t getmaxrss(void)
{
#if HAVE_GETRUSAGE && HAVE_STRUCT_RUSAGE_RU_MAXRSS
   struct rusage rusage;
   getrusage(RUSAGE_SELF, &rusage);
   return (int64_t)rusage.ru_maxrss * 1024;
#elif HAVE_GETPROCESSMEMORYINFO
   HANDLE proc;
   PROCESS_MEMORY_COUNTERS memcounters;
   proc = GetCurrentProcess();
   memcounters.cb = sizeof(memcounters);
   GetProcessMemoryInfo(proc, &memcounters, sizeof(memcounters));
   return memcounters.PeakPagefileUsage;
#else
   return 0;
#endif
}

static void log_callback_null(void *ptr, int level, const char *fmt, va_list vl)
{
}

int main(int argc, char **argv)
{
   int i, ret;
   int64_t ti;

   init_dynload();

   register_exit(ffmpeg_cleanup);

   setvbuf(stderr, NULL, _IONBF, 0); /* win32 runtime needs this */

   av_log_set_flags(AV_LOG_SKIP_REPEATED);
   parse_loglevel(argc, argv, options);

   if (argc > 1 && !strcmp(argv[1], "-d"))
   {
      run_as_daemon = 1;
      av_log_set_callback(log_callback_null);
      argc--;
      argv++;
   }

   avcodec_register_all();
#if CONFIG_AVDEVICE
   avdevice_register_all();
#endif
   avfilter_register_all();
   av_register_all();
   avformat_network_init();

   show_banner(argc, argv, options);

   /* parse options and open all input/output files */
   ret = ffmpeg_parse_options(argc, argv);
   if (ret < 0)
      exit_program(1);

   if (nb_output_files <= 0 && nb_input_files == 0)
   {
      show_usage();
      av_log(NULL, AV_LOG_WARNING, "Use -h to get full help or, even better, run 'man %s'\n", program_name);
      exit_program(1);
   }

   /* file converter / grab */
   if (nb_output_files <= 0)
   {
      av_log(NULL, AV_LOG_FATAL, "At least one output file must be specified\n");
      exit_program(1);
   }

   //     if (nb_input_files == 0) {
   //         av_log(NULL, AV_LOG_FATAL, "At least one input file must be specified\n");
   //         exit_program(1);
   //     }

   for (i = 0; i < nb_output_files; i++)
   {
      if (strcmp(output_files[i]->ctx->oformat->name, "rtp"))
         want_sdp = 0;
   }

   current_time = ti = getutime();
   if (transcode() < 0)
      exit_program(1);
   ti = getutime() - ti;
   if (do_benchmark)
   {
      av_log(NULL, AV_LOG_INFO, "bench: utime=%0.3fs\n", ti / 1000000.0);
   }
   av_log(NULL, AV_LOG_DEBUG, "%" PRIu64 " frames successfully decoded, %" PRIu64 " decoding errors\n",
          decode_error_stat[0], decode_error_stat[1]);
   if ((decode_error_stat[0] + decode_error_stat[1]) * max_error_rate < decode_error_stat[1])
      exit_program(69);

   exit_program(received_nb_signals ? 255 : main_return_code);
   return main_return_code;
}
