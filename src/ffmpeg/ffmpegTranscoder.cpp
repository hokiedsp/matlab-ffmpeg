#include "ffmpegTranscoder.h"

extern "C" {
#include <libavutil/time.h>
}

using namespace ffmpeg;

#define ABORT_ON_FLAG_EMPTY_OUTPUT (1 << 0)

Transcoder::Transcoder()
    : received_sigterm(0), transcode_init_done(false)
//,received_nb_signals(0),ffmpeg_exited(0),main_return_code(0)
{
}

/*
 * The following code is the main loop of the file converter
 */
void Transcoder::transcode(void)
{
   int ret, i;
   AVFormatContext *os;
   OutputStream *ost;
   InputStream *ist;
   int64_t timer_start;
   int64_t total_packets_written = 0;

   ret = transcode_init();
   if (ret < 0)
      goto fail;

   timer_start = av_gettime_relative();

   // create a thread for each input file to read frames/packets (does not decode)
   init_input_threads();

   // loop until all frames are processed/encountered an error, or interrupted by user
   while (!received_sigterm)
   {
      int64_t cur_time = av_gettime_relative();

      /* if 'q' pressed, exits */
      if (stdin_interaction)
         if (check_keyboard_interaction(cur_time) < 0)
            break;

      /* check if there's any stream where output is still needed */
      if (!need_output())
      {
         av_log(NULL, AV_LOG_VERBOSE, "No more output streams to write to, finishing.\n");
         break;
      }

      ret = transcode_step();
      if (ret < 0 && ret != AVERROR_EOF)
      {
         char errbuf[128];
         av_strerror(ret, errbuf, sizeof(errbuf));

         av_log(NULL, AV_LOG_ERROR, "Error while filtering: %s\n", errbuf);
         break;
      }

      /* dump report by using the output first video and audio streams */
      // print_report(0, timer_start, cur_time);
   }

   free_input_threads();

   /* at the end of stream, we must flush the decoder buffers */
   for (ist = input_streams.begin(); ist < input_streams.end(); ist++)
   {
      if (!ist->file.eof_reached && ist->decoding_needed)
         ist->process_packet(NULL, 0);
   }
   flush_encoders();

   term_exit();

   /* write the trailer if needed and close file */
   for (auto of = output_files.begin(); of < output_files.end(); of++)
   {
      os = of->ctx;
      if (!of->header_written)
      {
         av_log(NULL, AV_LOG_ERROR,
                "Nothing was written into output file %d (%s), because "
                "at least one of its streams received no packets.\n",
                i, os->filename);
         continue;
      }
      if ((ret = av_write_trailer(os)) < 0)
         throw ffmpegException("Error writing trailer of " + std::string(os->filename) + ": " + std::string(av_err2str(ret)));
   }

   /* dump report by using the first video and audio streams */
   //print_report(1, timer_start, av_gettime_relative());

   /* close each encoder */
   for (auto ost = output_streams.begin(); ost < output_streams.end(); ost++)
   {
      if (ost->encoding_needed)
      {
         av_freep(&ost->enc_ctx->stats_in);
      }
      total_packets_written += ost->packets_written;
   }

   if (!total_packets_written && (abort_on_flags & ABORT_ON_FLAG_EMPTY_OUTPUT))
      throw ffmpegException("Empty output");

   /* close each decoder */
   for (auto ist = input_streams.begin(); ist < input_streams.end(); ist++)
      ist->close();

   av_buffer_unref(&hw_device_ctx);

   /* finished ! */
   ret = 0;

fail:
   free_input_threads();

   if (output_streams.size())
   {
      for (auto ost = output_streams.begin(); ost < output_streams.end(); ost++)
         ost->clear_stream();
   }
   return ret;
}

int Transcoder::transcode_init(void)
{
   int ret = 0;
   AVFormatContext *oc;
   std::string error;

   for (auto fg = filtergraphs.begin(); fg < filtergraphs.end(); fg++)
   {
      for (auto ofilter = fg->outputs.begin(); ofilter < fg->outputs.end(); ofilter++)
      {
         if (!ofilter->ost || ofilter->ost->source_index >= 0)
            continue;
         if (fg->inputs.size() != 1)
            continue;

         // set the output filter's source stream
         auto ist = input_streams.rbegin();
         for (; ist < input_streams.rend(); ist++)
            if (fg->inputs[0].ist == ist)
               break;
         ofilter->ost->source_ist = &*ist;
      }
   }

   /* init framerate emulation */
   for (auto ifile = input_files.begin(); ifile < input_files.end(); ifile++)
   {
      if (ifile->rate_emu)
         for (auto ist = ifile.streams.begin(); ist < ifile.streams.end(); ist++)
            ist->start = av_gettime_relative();
   }

   /* for each output stream, we compute the right encoding parameters */
   for (auto ost = output_streams.begin(); ost < output_streams.end(); ost++)
   {
      oc = ost->file.ctx;
      auto ist = ost->get_input_stream();

      if (ost->attachment_filename)
         continue;

      if (ist)
      {
         ost->st->disposition = ist->st->disposition;
      }
      else
      {
         int j = 0;
         for (; j < oc->nb_streams; j++)
         {
            AVStream *st = oc->streams[j];
            if (st != ost->st && st->codecpar->codec_type == ost->st->codecpar->codec_type)
               break;
         }
         if (j == oc->nb_streams)
            if (ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO ||
                ost->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
               ost->st->disposition = AV_DISPOSITION_DEFAULT;
      }

      if (!ost->stream_copy) // must decode
      {
         AVCodecContext *enc_ctx = ost->enc_ctx;
         AVCodecContext *dec_ctx = NULL;

         ost->file.set_encoder_id(ost);

         if (ist)
         {
            dec_ctx = ist->dec_ctx;

            enc_ctx->chroma_sample_location = dec_ctx->chroma_sample_location;
         }

         if ((enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO || enc_ctx->codec_type == AVMEDIA_TYPE_AUDIO) &&
             ost->filter->graph->filtergraph_is_simple() && ost->filter->graph->configure_filtergraph())
            throw ffmpegException("Error opening filters!");

         if (enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
         {
            if (!ost->frame_rate.num)
               ost->frame_rate = av_buffersink_get_frame_rate(ost->filter->filter);
            if (ist && !ost->frame_rate.num)
               ost->frame_rate = ist->framerate;
            if (ist && !ost->frame_rate.num)
               ost->frame_rate = ist->st->r_frame_rate;
            if (ist && !ost->frame_rate.num)
            {
               ost->frame_rate = (AVRational){25, 1};
               av_log(NULL, AV_LOG_WARNING,
                      "No information "
                      "about the input framerate is available. Falling "
                      "back to a default value of 25fps for output stream #%d:%d. Use the -r option "
                      "if you want a different framerate.\n",
                      ost->file_index, ost->index);
            }
            //                    ost->frame_rate = ist->st->avg_frame_rate.num ? ist->st->avg_frame_rate : (AVRational){25, 1};
            if (ost->enc && ost->enc->supported_framerates && !ost->force_fps)
            {
               int idx = av_find_nearest_q_idx(ost->frame_rate, ost->enc->supported_framerates);
               ost->frame_rate = ost->enc->supported_framerates[idx];
            }
            // reduce frame rate for mpeg4 to be within the spec limits
            if (enc_ctx->codec_id == AV_CODEC_ID_MPEG4)
            {
               av_reduce(&ost->frame_rate.num, &ost->frame_rate.den,
                         ost->frame_rate.num, ost->frame_rate.den, 65535);
            }
         }

         switch (enc_ctx->codec_type)
         {
         case AVMEDIA_TYPE_AUDIO:
            enc_ctx->sample_fmt = ost->filter->filter->inputs[0]->format;
            if (dec_ctx)
               enc_ctx->bits_per_raw_sample = FFMIN(dec_ctx->bits_per_raw_sample,
                                                    av_get_bytes_per_sample(enc_ctx->sample_fmt) << 3);
            enc_ctx->sample_rate = ost->filter->filter->inputs[0]->sample_rate;
            enc_ctx->channel_layout = ost->filter->filter->inputs[0]->channel_layout;
            enc_ctx->channels = avfilter_link_get_channels(ost->filter->filter->inputs[0]);
            enc_ctx->time_base = (AVRational){1, enc_ctx->sample_rate};
            break;
         case AVMEDIA_TYPE_VIDEO:
            enc_ctx->time_base = av_inv_q(ost->frame_rate);
            if (!(enc_ctx->time_base.num && enc_ctx->time_base.den))
               enc_ctx->time_base = ost->filter->filter->inputs[0]->time_base;
            if (av_q2d(enc_ctx->time_base) < 0.001 && video_sync_method != VSYNC_PASSTHROUGH && (video_sync_method == VSYNC_CFR || video_sync_method == VSYNC_VSCFR || (video_sync_method == VSYNC_AUTO && !(oc->oformat->flags & AVFMT_VARIABLE_FPS))))
            {
               av_log(oc, AV_LOG_WARNING, "Frame rate very high for a muxer not efficiently supporting it.\n"
                                          "Please consider specifying a lower framerate, a different muxer or -vsync 2\n");
            }
            for (int j = 0; j < ost->forced_kf_count; j++)
               ost->forced_kf_pts[j] = av_rescale_q(ost->forced_kf_pts[j],
                                                    AV_TIME_BASE_Q,
                                                    enc_ctx->time_base);

            enc_ctx->width = ost->filter->filter->inputs[0]->w;
            enc_ctx->height = ost->filter->filter->inputs[0]->h;
            enc_ctx->sample_aspect_ratio = ost->st->sample_aspect_ratio =
                ost->frame_aspect_ratio.num ? // overridden by the -aspect cli option
                    av_mul_q(ost->frame_aspect_ratio, (AVRational){enc_ctx->height, enc_ctx->width})
                                            : ost->filter->filter->inputs[0]->sample_aspect_ratio;
            if (!strncmp(ost->enc->name, "libx264", 7) &&
                enc_ctx->pix_fmt == AV_PIX_FMT_NONE &&
                ost->filter->filter->inputs[0]->format != AV_PIX_FMT_YUV420P)
               av_log(NULL, AV_LOG_WARNING,
                      "No pixel format specified, %s for H.264 encoding chosen.\n"
                      "Use -pix_fmt yuv420p for compatibility with outdated media players.\n",
                      av_get_pix_fmt_name(ost->filter->filter->inputs[0]->format));
            if (!strncmp(ost->enc->name, "mpeg2video", 10) &&
                enc_ctx->pix_fmt == AV_PIX_FMT_NONE &&
                ost->filter->filter->inputs[0]->format != AV_PIX_FMT_YUV420P)
               av_log(NULL, AV_LOG_WARNING,
                      "No pixel format specified, %s for MPEG-2 encoding chosen.\n"
                      "Use -pix_fmt yuv420p for compatibility with outdated media players.\n",
                      av_get_pix_fmt_name(ost->filter->filter->inputs[0]->format));
            enc_ctx->pix_fmt = ost->filter->filter->inputs[0]->format;
            if (dec_ctx)
               enc_ctx->bits_per_raw_sample = FFMIN(dec_ctx->bits_per_raw_sample,
                                                    av_pix_fmt_desc_get(enc_ctx->pix_fmt)->comp[0].depth);

            ost->st->avg_frame_rate = ost->frame_rate;

            if (!dec_ctx ||
                enc_ctx->width != dec_ctx->width ||
                enc_ctx->height != dec_ctx->height ||
                enc_ctx->pix_fmt != dec_ctx->pix_fmt)
            {
               enc_ctx->bits_per_raw_sample = frame_bits_per_raw_sample;
            }

            if (ost->forced_keyframes)
            {
               if (!strncmp(ost->forced_keyframes, "expr:", 5))
               {
                  ret = av_expr_parse(&ost->forced_keyframes_pexpr, ost->forced_keyframes + 5,
                                      forced_keyframes_const_names, NULL, NULL, NULL, NULL, 0, NULL);
                  if (ret < 0)
                  {
                     av_log(NULL, AV_LOG_ERROR,
                            "Invalid force_key_frames expression '%s'\n", ost->forced_keyframes + 5);
                     return ret;
                  }
                  ost->forced_keyframes_expr_const_values[FKF_N] = 0;
                  ost->forced_keyframes_expr_const_values[FKF_N_FORCED] = 0;
                  ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_N] = NAN;
                  ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_T] = NAN;

                  // Don't parse the 'forced_keyframes' in case of 'keep-source-keyframes',
                  // parse it only for static kf timings
               }
               else if (strncmp(ost->forced_keyframes, "source", 6))
               {
                  ost->parse_forced_key_frames(ost->forced_keyframes, ost->enc_ctx);
               }
            }
            break;
         case AVMEDIA_TYPE_SUBTITLE:
            enc_ctx->time_base = (AVRational){1, 1000};
            if (!enc_ctx->width)
            {
               enc_ctx->width = input_streams[ost->source_index]->st->codecpar->width;
               enc_ctx->height = input_streams[ost->source_index]->st->codecpar->height;
            }
            break;
         case AVMEDIA_TYPE_DATA:
            break;
         default:
            throw ffmpegException("Failed to initialize the transcoder.");
         }
      }

      // if (ost->disposition)
      // {
      //    static const AVOption opts[] = {
      //        {"disposition", NULL, 0, AV_OPT_TYPE_FLAGS, {.i64 = 0}, INT64_MIN, INT64_MAX, .unit = "flags"},
      //        {"default", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AV_DISPOSITION_DEFAULT}, .unit = "flags"},
      //        {"dub", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AV_DISPOSITION_DUB}, .unit = "flags"},
      //        {"original", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AV_DISPOSITION_ORIGINAL}, .unit = "flags"},
      //        {"comment", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AV_DISPOSITION_COMMENT}, .unit = "flags"},
      //        {"lyrics", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AV_DISPOSITION_LYRICS}, .unit = "flags"},
      //        {"karaoke", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AV_DISPOSITION_KARAOKE}, .unit = "flags"},
      //        {"forced", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AV_DISPOSITION_FORCED}, .unit = "flags"},
      //        {"hearing_impaired", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AV_DISPOSITION_HEARING_IMPAIRED}, .unit = "flags"},
      //        {"visual_impaired", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AV_DISPOSITION_VISUAL_IMPAIRED}, .unit = "flags"},
      //        {"clean_effects", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AV_DISPOSITION_CLEAN_EFFECTS}, .unit = "flags"},
      //        {"captions", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AV_DISPOSITION_CAPTIONS}, .unit = "flags"},
      //        {"descriptions", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AV_DISPOSITION_DESCRIPTIONS}, .unit = "flags"},
      //        {"metadata", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AV_DISPOSITION_METADATA}, .unit = "flags"},
      //        {NULL},
      //    };
      //    static const AVClass class = {
      //        .class_name = "",
      //        .item_name = av_default_item_name,
      //        .option = opts,
      //        .version = LIBAVUTIL_VERSION_INT,
      //    };
      //    const AVClass *pclass = &class;

      //    ret = av_opt_eval_flags(&pclass, &opts[0], ost->disposition, &ost->st->disposition);
      //    if (ret < 0)
      //       goto dump_format;
      // }
   }

   /* init input streams */
   for (auto ist = input_streams.begin(); ist < input_streams.end(); ist++)
      if ((ret = ist->init_input_stream(error)) < 0)
      {
         for (auto ost = output_streams.begin(); ost < output_streams.end(); ost++)
            avcodec_close(ost->enc_ctx);

         goto dump_format;
      }

   /* open each encoder */
   for (auto ost = output_streams.begin(); ost < output_streams.end(); ost++)
   {
      ret = ost->init_output_stream(error);
      if (ret < 0)
         goto dump_format;
   }

   /* discard unused programs */
   for (auto ifile = input_files.begin(); ifile < input_files.end(); ifile++)
   {
      for (int j = 0; j < ifile->ctx->nb_programs; j++)
      {
         AVProgram *p = ifile->ctx->programs[j];
         int discard = AVDISCARD_ALL;

         for (int k = 0; k < p->nb_stream_indexes; k++)
            if (!ifile->streams[p->stream_index[k]].discard)
            {
               discard = AVDISCARD_DEFAULT;
               break;
            }
         p->discard = discard;
      }
   }

   /* write headers for files with no streams */
   for (auto ofile = output_files.begin(); ofile < output_files.end(); ofile++)
   {
      oc = ofile->ctx;
      if (oc->oformat->flags & AVFMT_NOSTREAMS && oc->nb_streams == 0)
      {
         ret = ofile->check_init_output_file(i);
         if (ret < 0)
            goto dump_format;
      }
   }

dump_format:
//    /* dump the stream mapping */
//    av_log(NULL, AV_LOG_INFO, "Stream mapping:\n");
//    for (auto ist = input_streams.begin(); ist < input_streams.end(); ist++)
//    {
//       for (int j = 0; j < ist->nb_filters; j++)
//       {
//          if (!ist->filters[j]->graph->filtergraph_is_simple())
//          {
//             av_log(NULL, AV_LOG_INFO, "  Stream #%d:%d (%s) -> %s",
//                    ist->file_index, ist->st->index, ist->dec ? ist->dec->name : "?",
//                    ist->filters[j]->name);
//             if (filtergraphs.size() > 1)
//                av_log(NULL, AV_LOG_INFO, " (graph %d)", ist->filters[j]->graph->index);
//             av_log(NULL, AV_LOG_INFO, "\n");
//          }
//       }
//    }

//    for (auto ost = output_streams.begin(); ost < output_streams.end(); ost++)
//    {
//       if (ost->attachment_filename)
//       {
//          /* an attached file */
//          av_log(NULL, AV_LOG_INFO, "  File %s -> Stream #%d:%d\n",
//                 ost->attachment_filename, ost->file_index, ost->index);
//          continue;
//       }

//       if (ost->filter && !ost->filter->graph->filtergraph_is_simple())
//       {
//          /* output from a complex graph */
//          av_log(NULL, AV_LOG_INFO, "  %s", ost->filter->name);
//          if (filtergraphs.size() > 1)
//             av_log(NULL, AV_LOG_INFO, " (graph %d)", ost->filter->graph->index);

//          av_log(NULL, AV_LOG_INFO, " -> Stream #%d:%d (%s)\n", ost->file_index,
//                 ost->index, ost->enc ? ost->enc->name : "?");
//          continue;
//       }

//       av_log(NULL, AV_LOG_INFO, "  Stream #%d:%d -> #%d:%d",
//              input_streams[ost->source_index]->file.index,
//              input_streams[ost->source_index]->st->index,
//              ost->file_index,
//              ost->index);
//       if (ost->sync_ist != input_streams[ost->source_index])
//          av_log(NULL, AV_LOG_INFO, " [sync #%d:%d]",
//                 ost->sync_ist->file_index,
//                 ost->sync_ist->st->index);
//       if (ost->stream_copy)
//          av_log(NULL, AV_LOG_INFO, " (copy)");
//       else
//       {
//          const AVCodec *in_codec = input_streams[ost->source_index]->dec;
//          const AVCodec *out_codec = ost->enc;
//          const char *decoder_name = "?";
//          const char *in_codec_name = "?";
//          const char *encoder_name = "?";
//          const char *out_codec_name = "?";
//          const AVCodecDescriptor *desc;

//          if (in_codec)
//          {
//             decoder_name = in_codec->name;
//             desc = avcodec_descriptor_get(in_codec->id);
//             if (desc)
//                in_codec_name = desc->name;
//             if (!strcmp(decoder_name, in_codec_name))
//                decoder_name = "native";
//          }

//          if (out_codec)
//          {
//             encoder_name = out_codec->name;
//             desc = avcodec_descriptor_get(out_codec->id);
//             if (desc)
//                out_codec_name = desc->name;
//             if (!strcmp(encoder_name, out_codec_name))
//                encoder_name = "native";
//          }

//          av_log(NULL, AV_LOG_INFO, " (%s (%s) -> %s (%s))",
//                 in_codec_name, decoder_name,
//                 out_codec_name, encoder_name);
//       }
//       av_log(NULL, AV_LOG_INFO, "\n");
//    }

   if (ret)
   {
      av_log(NULL, AV_LOG_ERROR, "%s\n", error);
      return ret;
   }

   transcode_init_done = true;

   return 0;
}

/* Return 1 if there remain streams where more output is wanted, 0 otherwise. */
bool Transcoder::need_output(void)
{
   for (auto ost = output_streams.begin(); ost < output_streams.end(); ost++)
   {
      OutputFile &of = ost->file;
      AVFormatContext *os = of.ctx;

      if (ost->finished || (os->pb && avio_tell(os->pb) >= of.limit_filesize))
         continue;
      if (ost->frame_number >= ost->max_frames)
      {
         for (auto ost = of.streams.begin(); ost < of.streams.end(); ost++)
            ost->close_output_stream();
         continue;
      }

      return true;
   }

   return false;
}

/**
 * Run a single step of transcoding.
 *
 * @return  0 for success, <0 for error
 */
int Transcoder::transcode_step(void)
{
   int ret;

   // get the output stream with its previous packet being the oldest (i.e., most likely to be the next to be encoded)
   OutputStream *ost = choose_output();
   if (!ost)
   {
      if (got_eagain()) // frame was not ready
      {
         reset_eagain();
         av_usleep(10000);
         return 0;
      }
      av_log(NULL, AV_LOG_VERBOSE, "No more inputs to read from, finishing.\n");
      return AVERROR_EOF;
   }

   InputStream *ist;
   if (ost->filter)
   {
      if ((ret = transcode_from_filter(ost->filter->graph, ist)) < 0)
         return ret;
      if (!ist)
         return 0;
   }
   else
   {
      if (!ost->source_ist)
         throw ffmpegException("Source stream lost.");

      ist = ost->source_ist;
   }

   // get next packet
   AVPacket pkt;
   ret = ist->file.get_packet(&pkt); // if packet received, ret>=0; otherwise, not packet not ready or eof.  throws exception if errored
   if (ret>=0) // 
   {
      reset_eagain();

      ist->file.prepare_packet(pkt, ist);
      if (ist)
      {
         for (auto ost = output_streams.begin(); ost < output_Streams.end(); ost++)
         {
            if (check_output_constraints(ist, ost) && !ost->encoding_needed)
               do_streamcopy(ist, ost, pkt);
         }
      }
      av_packet_unref(&pkt);
   }
   else if (ret == AVERROR(EAGAIN))
   {
      if (ist->file.eagain)
         ost->unavailable = 1;
      return 0;
   }

   // encode frame or copy the packet if copying

   if (ret < 0)
      return ret == AVERROR_EOF ? 0 : ret;

   return reap_filters(0);
}

void *Transcoder::input_thread(void *arg)
{
   InputFile *f = (InputFile *)arg;
   unsigned flags = f->non_blocking ? AV_THREAD_MESSAGE_NONBLOCK : 0;
   int ret = 0;

   while (1)
   {
      AVPacket pkt;
      ret = av_read_frame(f->ctx, &pkt);

      if (ret == AVERROR(EAGAIN))
      {
         av_usleep(10000);
         continue;
      }
      if (ret < 0)
      {
         av_thread_message_queue_set_err_recv(f->in_thread_queue, ret);
         break;
      }
      ret = av_thread_message_queue_send(f->in_thread_queue, &pkt, flags);
      if (flags && ret == AVERROR(EAGAIN))
      {
         flags = 0;
         ret = av_thread_message_queue_send(f->in_thread_queue, &pkt, flags);
         av_log(f->ctx, AV_LOG_WARNING,
                "Thread message queue blocking; consider raising the "
                "thread_queue_size option (current value: %d)\n",
                f->thread_queue_size);
      }
      if (ret < 0)
      {
         if (ret != AVERROR_EOF)
            av_log(f->ctx, AV_LOG_ERROR,
                   "Unable to send packet to main thread: %s\n",
                   av_err2str(ret));
         av_packet_unref(&pkt);
         av_thread_message_queue_set_err_recv(f->in_thread_queue, ret);
         break;
      }
   }

   return NULL;
}

void Transcoder::free_input_threads(void)
{
   for (auto f = input_files.begin(); f < input_files.end(); f++)
      f->free_thread();
}

void Transcoder::init_input_threads(void)
{
   for (auto f = input_files.begin(); f < input_files.end(); f++)
      f->init_thread();
}

void Transcoder::flush_encoders(void)
{
   int i, ret;

   for (auto ost = output_streams.begin(); ost < output_streams.end(); ost++)
   {
      AVCodecContext *enc = ost->enc_ctx;
      OutputFile *of = output_files[ost->file_index];
      int stop_encoding = 0;

      if (!ost->encoding_needed)
         continue;

      if (enc->codec_type == AVMEDIA_TYPE_AUDIO && enc->frame_size <= 1)
         continue;
#if FF_API_LAVF_FMT_RAWPICTURE
      if (enc->codec_type == AVMEDIA_TYPE_VIDEO && (of->ctx->oformat->flags & AVFMT_RAWPICTURE) && enc->codec->id == AV_CODEC_ID_RAWVIDEO)
         continue;
#endif

      if (enc->codec_type != AVMEDIA_TYPE_VIDEO && enc->codec_type != AVMEDIA_TYPE_AUDIO)
         continue;

      avcodec_send_frame(enc, NULL);

      for (;;)
      {
         const char *desc = NULL;

         switch (enc->codec_type)
         {
         case AVMEDIA_TYPE_AUDIO:
            desc = "audio";
            break;
         case AVMEDIA_TYPE_VIDEO:
            desc = "video";
            break;
         default:
            throw ffmpegException("Unsupported media type to encode.");
         }

         if (1)
         {
            AVPacket pkt;
            int pkt_size;
            av_init_packet(&pkt);
            pkt.data = NULL;
            pkt.size = 0;

            ret = avcodec_receive_packet(enc, &pkt);
            if (ret < 0 && ret != AVERROR_EOF)
               throw ffmpegException(desc + std::string(" encoding failed: ") + av_err2str(ret));

            if (ost->logfile && enc->stats_out)
               ost->logfile << enc->stats_out;
            
            if (ret == AVERROR_EOF)
            {
               stop_encoding = 1;
               break;
            }
            if (ost->finished & MUXER_FINISHED)
            {
               av_packet_unref(&pkt);
               continue;
            }
            av_packet_rescale_ts(&pkt, enc->time_base, ost->st->time_base);
            pkt_size = pkt.size;
            of->output_packet(&pkt, &*ost);
            if (ost->enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO && vstats_filename)
            {
               ost->do_video_stats(pkt_size);
            }
         }

         if (stop_encoding)
            break;
      }
   }
}

/**
 * Select the output stream to process.
 *
 * @return  selected output stream, or NULL if none available
 */
OutputStream *Transcoder::choose_output(void)
{
   int i;
   int64_t opts_min = INT64_MAX;
   OutputStream *ost_min = NULL;

   for (auto ost = output_streams.begin(); ost < output_streams.end(); ost++)
   {
      int64_t opts = ost->st->cur_dts == AV_NOPTS_VALUE ? INT64_MIN : av_rescale_q(ost->st->cur_dts, ost->st->time_base, AV_TIME_BASE_Q);
      if (ost->st->cur_dts == AV_NOPTS_VALUE)
         av_log(NULL, AV_LOG_DEBUG, "cur_dts is invalid (this is harmless if it occurs once at the start per stream)\n");

      if (!ost->finished && opts < opts_min)
      {
         opts_min = opts;
         ost_min = ost->unavailable ? NULL : &*ost;
      }
   }
   return ost_min;
}

/////////////////

bool Transcoder::got_eagain(void)
{
   for (auto ost = output_streams.begin(); ost < output_streams.end(); ost++)
      if (ost->unavailable)
         return true;
   return false;
}

void Transcoder::reset_eagain(void)
{
   for (auto ifile = input_files.begin(); ifile < input_files.end(); ifile++)
      ifile->eagain = 0;
   for (auto ost = output_streams.begin(); ost < output_streams.end(); ost++)
      ost->unavailable = 0;
}

//////////////////////////////////////////////////////////////

/**
 * Perform a step of transcoding for the specified filter graph.
 *
 * @param[in]  graph     filter graph to consider
 * @param[out] best_ist  input stream where a frame would allow to continue
 * @return  0 for success, <0 for error
 */
int Transcoder::transcode_from_filter(FilterGraph &graph, InputStream *&best_ist)
{
   int ret;

   // Request a frame on the oldest sink link.
   if ((ret = avfilter_graph_request_oldest(graph.graph)) >= 0)
      return reap_filters(0);
   if (ret == AVERROR_EOF)
   {
      ret = reap_filters(1);
      for (auto ofilter = graph.outputs.begin(); ofilter < graph.outputs.end(); ofilter++)
         ofilter.ost->close_output_stream();
      return ret;
   }
   if (ret != AVERROR(EAGAIN))
      return ret;

   // look for the best matching input stream: the one with the most failed requests ????
   best_ist = NULL;
   int nb_requests_max = 0;
   for (auto ifilter = graph.inputs.begin(); ifilter < graph.inputs.end(); ifilter++)
   {
      InputStream *ist = &ifilter->ist;
      if (ist->file.eagain || ist->file.eof_reached)
         continue;

      int nb_requests = av_buffersrc_get_nb_failed_requests(ifilter->filter);
      if (nb_requests > nb_requests_max)
      {
         nb_requests_max = nb_requests;
         best_ist = ist;
      }
   }

   if (!best_ist)
      for (auto ofilter = graph.outputs.begin(); ofilter < graph.outputs.end(); ofilter++)
         ofilter->ost->unavailable = 1;

   return 0;
}

//////////////////////////////////////////////////////

/**
 * Get and encode new output from any of the filtergraphs, without causing
 * activity.
 *
 * @return  0 for success, <0 for severe errors
 */
int Transcoder::reap_filters(int flush)
{
   AVFrame *filtered_frame = NULL;
   int i;

   /* Reap all buffers present in the buffer sinks */
   for (auto ost = output_streams.begin(); ost < output_streams.end(); ost++)
   {
      OutputFile &of = ost->file;
      AVFilterContext *filter;
      AVCodecContext *enc = ost->enc_ctx;
      int ret = 0;

      if (!ost->filter)
         continue;
      filter = ost->filter->filter;

      if (!(ost->filtered_frame || ost->filtered_frame = av_frame_alloc()))
         return AVERROR(ENOMEM);
      filtered_frame = ost->filtered_frame;

      while (1)
      {
         double float_pts = AV_NOPTS_VALUE; // this is identical to filtered_frame.pts but with higher precision
         ret = av_buffersink_get_frame_flags(filter, filtered_frame, AV_BUFFERSINK_FLAG_NO_REQUEST);
         if (ret < 0)
         {
            if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            {
               av_log(NULL, AV_LOG_WARNING, "Error in av_buffersink_get_frame_flags(): %s\n", av_err2str(ret));
            }
            else if (flush && ret == AVERROR_EOF)
            {
               if (filter->inputs[0]->type == AVMEDIA_TYPE_VIDEO)
                  of.do_video_out(ost, NULL, AV_NOPTS_VALUE);
            }
            break;
         }
         if (ost->finished)
         {
            av_frame_unref(filtered_frame);
            continue;
         }
         if (filtered_frame->pts != AV_NOPTS_VALUE)
         {
            int64_t start_time = (of.start_time == AV_NOPTS_VALUE) ? 0 : of.start_time;
            AVRational tb = enc->time_base;
            int extra_bits = av_clip(29 - av_log2(tb.den), 0, 16);

            tb.den <<= extra_bits;
            float_pts =
                av_rescale_q(filtered_frame->pts, filter->inputs[0]->time_base, tb) -
                av_rescale_q(start_time, AV_TIME_BASE_Q, tb);
            float_pts /= 1 << extra_bits;
            // avoid exact midoints to reduce the chance of rounding differences, this can be removed in case the fps code is changed to work with integers
            float_pts += FFSIGN(float_pts) * 1.0 / (1 << 17);

            filtered_frame->pts =
                av_rescale_q(filtered_frame->pts, filter->inputs[0]->time_base, enc->time_base) -
                av_rescale_q(start_time, AV_TIME_BASE_Q, enc->time_base);
         }
         //if (ost->source_index >= 0)
         //    *filtered_frame= *input_streams[ost->source_index]->decoded_frame; //for me_threshold

         switch (filter->inputs[0]->type)
         {
         case AVMEDIA_TYPE_VIDEO:
            if (!ost->frame_aspect_ratio.num)
               enc->sample_aspect_ratio = filtered_frame->sample_aspect_ratio;

            of.do_video_out(ost, filtered_frame, float_pts);
            break;
         case AVMEDIA_TYPE_AUDIO:
            if (!(enc->codec->capabilities & AV_CODEC_CAP_PARAM_CHANGE) &&
                enc->channels != av_frame_get_channels(filtered_frame))
            {
               av_log(NULL, AV_LOG_ERROR,
                      "Audio filter graph output is not normalized and encoder does not support parameter changes\n");
               break;
            }
            of.do_audio_out(ost, filtered_frame);
            break;
         default:
            // TODO support subtitle filters
            throw ffmpegException("Unsupported media type to filter.");
         }

         av_frame_unref(filtered_frame);
      }
   }

   return 0;
}


/*
 * Check whether a packet from ist should be written into ost at this time
 */
bool Transcoder::check_output_constraints(InputStream *ist, OutputStream *ost)
{
   OutputFile &of = ost->file;

   if (ost->source != ist)
      return false;

   if (ost->finished)
      return false;

   if (of->start_time != AV_NOPTS_VALUE && ist->pts < of->start_time)
      return false;

   return true;
}

void Transcoder::do_streamcopy(InputStream *ist, OutputStream *ost, const AVPacket *pkt)
{
   OutputFile &of = ost->file;
   InputFile &f = ist->file;
   int64_t start_time = (of.start_time == AV_NOPTS_VALUE) ? 0 : of.start_time;
   int64_t ost_tb_start_time = av_rescale_q(start_time, AV_TIME_BASE_Q, ost->st->time_base);
   AVPicture pict;
   AVPacket opkt;

   av_init_packet(&opkt);

   if ((!ost->frame_number && !(pkt->flags & AV_PKT_FLAG_KEY)) && !ost->copy_initial_nonkeyframes)
      return;

   if (!ost->frame_number && !ost->copy_prior_start)
   {
      int64_t comp_start = start_time;
      if (copy_ts && f.start_time != AV_NOPTS_VALUE)
         comp_start = FFMAX(start_time, f.start_time + f.ts_offset);
      if (pkt->pts == AV_NOPTS_VALUE ? ist->pts < comp_start : pkt->pts < av_rescale_q(comp_start, AV_TIME_BASE_Q, ist->st->time_base))
         return;
   }

   if (of.recording_time != INT64_MAX && ist->pts >= of.recording_time + start_time)
   {
      ost->close();
      return;
   }

   if (f.recording_time != INT64_MAX)
   {
      start_time = f.ctx->start_time;
      if (f.start_time != AV_NOPTS_VALUE && copy_ts)
         start_time += f.start_time;
      if (ist->pts >= f.recording_time + start_time)
      {
         ost->close();
         return;
      }
   }

   /* force the input stream PTS */
   if (ost->enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
      ost->sync_opts++;

   if (pkt->pts != AV_NOPTS_VALUE)
      opkt.pts = av_rescale_q(pkt->pts, ist->st->time_base, ost->st->time_base) - ost_tb_start_time;
   else
      opkt.pts = AV_NOPTS_VALUE;

   if (pkt->dts == AV_NOPTS_VALUE)
      opkt.dts = av_rescale_q(ist->dts, AV_TIME_BASE_Q, ost->st->time_base);
   else
      opkt.dts = av_rescale_q(pkt->dts, ist->st->time_base, ost->st->time_base);
   opkt.dts -= ost_tb_start_time;

   if (ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && pkt->dts != AV_NOPTS_VALUE)
   {
      int duration = av_get_audio_frame_duration(ist->dec_ctx, pkt->size);
      if (!duration)
         duration = ist->dec_ctx->frame_size;
      opkt.dts = opkt.pts = av_rescale_delta(ist->st->time_base, pkt->dts,
                                             (AVRational){1, ist->dec_ctx->sample_rate}, duration, &ist->filter_in_rescale_delta_last,
                                             ost->st->time_base) -
                            ost_tb_start_time;
   }

   opkt.duration = av_rescale_q(pkt->duration, ist->st->time_base, ost->st->time_base);
   opkt.flags = pkt->flags;
   // FIXME remove the following 2 lines they shall be replaced by the bitstream filters
   if (ost->st->codecpar->codec_id != AV_CODEC_ID_H264 && ost->st->codecpar->codec_id != AV_CODEC_ID_MPEG1VIDEO && ost->st->codecpar->codec_id != AV_CODEC_ID_MPEG2VIDEO && ost->st->codecpar->codec_id != AV_CODEC_ID_VC1)
   {
      int ret = av_parser_change(ost->parser, ost->parser_avctx, &opkt.data, &opkt.size, pkt->data, pkt->size, pkt->flags & AV_PKT_FLAG_KEY);
      if (ret < 0)
         throw ffmpegException("av_parser_change failed: " + std::string(av_err2str(ret)));

      if (ret)
      {
         opkt.buf = av_buffer_create(opkt.data, opkt.size, av_buffer_default_free, NULL, 0);
         if (!opkt.buf)
            throw ffmpegException("av_buffer_create failed to allocate memoery.");
      }
   }
   else
   {
      opkt.data = pkt->data;
      opkt.size = pkt->size;
   }
   av_copy_packet_side_data(&opkt, pkt);

   output_packet(of, &opkt, ost);
}
