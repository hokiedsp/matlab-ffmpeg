
#include "ffmpegOutputFile.h"

#include "ffmpegException.h"

using namespace ffmpeg;

// open an multimedia file with path specified by filename as the i-th output file
// with options given in o
OutputFile::OutputFile(const std::string filename, const int i, OptionsContext &o)
    : index(i), ctx(NULL, delete_format_ctx)
{
   recording_time = o.recording_time;
   start_time = o.start_time;
   limit_filesize = o.limit_filesize;
   shortest = o.shortest;
   av_dict_copy(&opts, o.g->format_opts, 0);

   int err = avformat_alloc_output_context2(&ctx, NULL, o.format, filename);
   if (!ctx)
      throw ffmpegException(filename, err);

   if (o.recording_time != INT64_MAX)
      ctx->duration = o.recording_time;

   file_oformat = ctx->oformat;
   ctx->interrupt_callback = ffmpegBase::int_cb;
}

////////////////////////////////////////////////

void OutputFile::set_encoder_id(OutputStream &ost)
{
   AVDictionaryEntry *e;

   uint8_t *encoder_string;
   int encoder_string_len;
   int format_flags = 0;
   int codec_flags = 0;

   if (av_dict_get(ost->st->metadata, "encoder", NULL, 0))
      return;

   e = av_dict_get(of->opts, "fflags", NULL, 0);
   if (e)
   {
      const AVOption *o = av_opt_find(of->ctx, "fflags", NULL, 0, 0);
      if (!o)
         return;
      av_opt_eval_flags(of->ctx, o, e->value, &format_flags);
   }
   e = av_dict_get(ost->encoder_opts, "flags", NULL, 0);
   if (e)
   {
      const AVOption *o = av_opt_find(ost->enc_ctx, "flags", NULL, 0, 0);
      if (!o)
         return;
      av_opt_eval_flags(ost->enc_ctx, o, e->value, &codec_flags);
   }

   encoder_string_len = sizeof(LIBAVCODEC_IDENT) + strlen(ost->enc->name) + 2;
   encoder_string = av_mallocz(encoder_string_len);
   if (!encoder_string)
      exit_program(1);

   if (!(format_flags & AVFMT_FLAG_BITEXACT) && !(codec_flags & AV_CODEC_FLAG_BITEXACT))
      av_strlcpy(encoder_string, LIBAVCODEC_IDENT " ", encoder_string_len);
   else
      av_strlcpy(encoder_string, "Lavc ", encoder_string_len);
   av_strlcat(encoder_string, ost->enc->name, encoder_string_len);
   av_dict_set(&ost->st->metadata, "encoder", encoder_string,
               AV_DICT_DONT_STRDUP_VAL | AV_DICT_DONT_OVERWRITE);
}

///////////////////////////////////////

/* open the muxer when all the streams are initialized */
int check_init_output_file(int file_index)
{
   int ret, i;

   for (i = 0; i < of->ctx->nb_streams; i++)
   {
      OutputStream *ost = output_streams[of->ost_index + i];
      if (!ost->initialized)
         return 0;
   }

   of->ctx->interrupt_callback = int_cb;

   ret = avformat_write_header(of->ctx, &of->opts);
   if (ret < 0)
   {
      av_log(NULL, AV_LOG_ERROR,
             "Could not write header for output file #%d "
             "(incorrect codec parameters ?): %s",
             file_index, av_err2str(ret));
      return ret;
   }
   //assert_avoptions(of->opts);
   of->header_written = 1;

   av_dump_format(of->ctx, file_index, of->ctx->filename, 1);

   if (sdp_filename || want_sdp)
      print_sdp();

   /* flush the muxing queues */
   for (i = 0; i < of->ctx->nb_streams; i++)
   {
      OutputStream *ost = output_streams[of->ost_index + i];

      while (av_fifo_size(ost->muxing_queue))
      {
         AVPacket pkt;
         av_fifo_generic_read(ost->muxing_queue, &pkt, sizeof(pkt), NULL);
         write_packet(of, &pkt, ost);
      }
   }

   return 0;
}
////////////////////////////////////////////

void OutputFile::output_packet(AVPacket *pkt, OutputStream *ost)
{
   int ret = 0;

   /* apply the output bitstream filters, if any */
   if (ost->nb_bitstream_filters)
   {
      int idx;

      av_packet_split_side_data(pkt);
      ret = av_bsf_send_packet(ost->bsf_ctx[0], pkt);
      if (ret < 0)
         goto finish;

      idx = 1;
      while (idx)
      {
         /* get a packet from the previous filter up the chain */
         ret = av_bsf_receive_packet(ost->bsf_ctx[idx - 1], pkt);
         /* HACK! - aac_adtstoasc updates extradata after filtering the first frame when
             * the api states this shouldn't happen after init(). Propagate it here to the
             * muxer and to the next filters in the chain to workaround this.
             * TODO/FIXME - Make aac_adtstoasc use new packet side data instead of changing
             * par_out->extradata and adapt muxers accordingly to get rid of this. */
         if (!(ost->bsf_extradata_updated[idx - 1] & 1))
         {
            ret = avcodec_parameters_copy(ost->st->codecpar, ost->bsf_ctx[idx - 1]->par_out);
            if (ret < 0)
               goto finish;
            ost->bsf_extradata_updated[idx - 1] |= 1;
         }
         if (ret == AVERROR(EAGAIN))
         {
            ret = 0;
            idx--;
            continue;
         }
         else if (ret < 0)
            goto finish;

         /* send it to the next filter down the chain or to the muxer */
         if (idx < ost->nb_bitstream_filters)
         {
            /* HACK/FIXME! - See above */
            if (!(ost->bsf_extradata_updated[idx] & 2))
            {
               ret = avcodec_parameters_copy(ost->bsf_ctx[idx]->par_out, ost->bsf_ctx[idx - 1]->par_out);
               if (ret < 0)
                  goto finish;
               ost->bsf_extradata_updated[idx] |= 2;
            }
            ret = av_bsf_send_packet(ost->bsf_ctx[idx], pkt);
            if (ret < 0)
               goto finish;
            idx++;
         }
         else
            write_packet(of, pkt, ost);
      }
   }
   else
      write_packet(of, pkt, ost);

finish:
   if (ret < 0 && ret != AVERROR_EOF)
   {
      av_log(NULL, AV_LOG_ERROR, "Error applying bitstream filters to an output "
                                 "packet for stream #%d:%d.\n",
             ost->file_index, ost->index);
      if (exit_on_error)
         exit_program(1);
   }
}

///////////////////////////////////////////////////////////

static void do_video_out(OutputFile *of, OutputStream *ost, AVFrame *next_picture, double sync_ipts)
{
   int ret, format_video_sync;
   AVPacket pkt;
   AVCodecContext *enc = ost->enc_ctx;
   AVCodecParameters *mux_par = ost->st->codecpar;
   int nb_frames, nb0_frames, i;
   double delta, delta0;
   double duration = 0;
   int frame_size = 0;
   InputStream *ist = NULL;
   AVFilterContext *filter = ost->filter->filter;

   if (ost->source_index >= 0)
      ist = input_streams[ost->source_index];

   if (filter->inputs[0]->frame_rate.num > 0 &&
       filter->inputs[0]->frame_rate.den > 0)
      duration = 1 / (av_q2d(filter->inputs[0]->frame_rate) * av_q2d(enc->time_base));

   if (ist && ist->st->start_time != AV_NOPTS_VALUE && ist->st->first_dts != AV_NOPTS_VALUE && ost->frame_rate.num)
      duration = FFMIN(duration, 1 / (av_q2d(ost->frame_rate) * av_q2d(enc->time_base)));

   if (!ost->filters_script &&
       !ost->filters &&
       next_picture &&
       ist &&
       lrintf(av_frame_get_pkt_duration(next_picture) * av_q2d(ist->st->time_base) / av_q2d(enc->time_base)) > 0)
   {
      duration = lrintf(av_frame_get_pkt_duration(next_picture) * av_q2d(ist->st->time_base) / av_q2d(enc->time_base));
   }

   if (!next_picture)
   {
      //end, flushing
      nb0_frames = nb_frames = mid_pred(ost->last_nb0_frames[0],
                                        ost->last_nb0_frames[1],
                                        ost->last_nb0_frames[2]);
   }
   else
   {
      delta0 = sync_ipts - ost->sync_opts; // delta0 is the "drift" between the input frame (next_picture) and where it would fall in the output.
      delta = delta0 + duration;

      /* by default, we output a single frame */
      nb0_frames = 0; // tracks the number of times the PREVIOUS frame should be duplicated, mostly for variable framerate (VFR)
      nb_frames = 1;

      format_video_sync = video_sync_method;
      if (format_video_sync == VSYNC_AUTO)
      {
         if (!strcmp(of->ctx->oformat->name, "avi"))
         {
            format_video_sync = VSYNC_VFR;
         }
         else
            format_video_sync = (of->ctx->oformat->flags & AVFMT_VARIABLE_FPS) ? ((of->ctx->oformat->flags & AVFMT_NOTIMESTAMPS) ? VSYNC_PASSTHROUGH : VSYNC_VFR) : VSYNC_CFR;
         if (ist && format_video_sync == VSYNC_CFR && input_files[ist->file_index]->ctx->nb_streams == 1 && input_files[ist->file_index]->input_ts_offset == 0)
         {
            format_video_sync = VSYNC_VSCFR;
         }
         if (format_video_sync == VSYNC_CFR && copy_ts)
         {
            format_video_sync = VSYNC_VSCFR;
         }
      }
      ost->is_cfr = (format_video_sync == VSYNC_CFR || format_video_sync == VSYNC_VSCFR);

      if (delta0 < 0 &&
          delta > 0 &&
          format_video_sync != VSYNC_PASSTHROUGH &&
          format_video_sync != VSYNC_DROP)
      {
         if (delta0 < -0.6)
         {
            av_log(NULL, AV_LOG_WARNING, "Past duration %f too large\n", -delta0);
         }
         else
            av_log(NULL, AV_LOG_DEBUG, "Clipping frame in rate conversion by %f\n", -delta0);
         sync_ipts = ost->sync_opts;
         duration += delta0;
         delta0 = 0;
      }

      switch (format_video_sync)
      {
      case VSYNC_VSCFR:
         if (ost->frame_number == 0 && delta0 >= 0.5)
         {
            av_log(NULL, AV_LOG_DEBUG, "Not duplicating %d initial frames\n", (int)lrintf(delta0));
            delta = duration;
            delta0 = 0;
            ost->sync_opts = lrint(sync_ipts);
         }
      case VSYNC_CFR:
         // FIXME set to 0.5 after we fix some dts/pts bugs like in avidec.c
         if (frame_drop_threshold && delta < frame_drop_threshold && ost->frame_number)
         {
            nb_frames = 0;
         }
         else if (delta < -1.1)
            nb_frames = 0;
         else if (delta > 1.1)
         {
            nb_frames = lrintf(delta);
            if (delta0 > 1.1)
               nb0_frames = lrintf(delta0 - 0.6);
         }
         break;
      case VSYNC_VFR:
         if (delta <= -0.6)
            nb_frames = 0;
         else if (delta > 0.6)
            ost->sync_opts = lrint(sync_ipts);
         break;
      case VSYNC_DROP:
      case VSYNC_PASSTHROUGH:
         ost->sync_opts = lrint(sync_ipts);
         break;
      default:
         av_assert0(0);
      }
   }

   nb_frames = FFMIN(nb_frames, ost->max_frames - ost->frame_number);
   nb0_frames = FFMIN(nb0_frames, nb_frames);

   memmove(ost->last_nb0_frames + 1,
           ost->last_nb0_frames,
           sizeof(ost->last_nb0_frames[0]) * (FF_ARRAY_ELEMS(ost->last_nb0_frames) - 1));
   ost->last_nb0_frames[0] = nb0_frames;

   if (nb0_frames == 0 && ost->last_dropped)
   {
      nb_frames_drop++;
      av_log(NULL, AV_LOG_VERBOSE,
             "*** dropping frame %d from stream %d at ts %" PRId64 "\n",
             ost->frame_number, ost->st->index, ost->last_frame->pts);
   }
   if (nb_frames > (nb0_frames && ost->last_dropped) + (nb_frames > nb0_frames))
   {
      if (nb_frames > dts_error_threshold * 30)
      {
         av_log(NULL, AV_LOG_ERROR, "%d frame duplication too large, skipping\n", nb_frames - 1);
         nb_frames_drop++;
         return;
      }
      nb_frames_dup += nb_frames - (nb0_frames && ost->last_dropped) - (nb_frames > nb0_frames);
      av_log(NULL, AV_LOG_VERBOSE, "*** %d dup!\n", nb_frames - 1);
   }
   ost->last_dropped = nb_frames == nb0_frames && next_picture;

   /* duplicates frame if needed */
   for (i = 0; i < nb_frames; i++)
   {
      AVFrame *in_picture;
      av_init_packet(&pkt);
      pkt.data = NULL;
      pkt.size = 0;

      if (i < nb0_frames && ost->last_frame)
      {
         in_picture = ost->last_frame;
      }
      else
         in_picture = next_picture;

      if (!in_picture)
         return;

      in_picture->pts = ost->sync_opts;

#if 1
      if (!check_recording_time(ost))
#else
      if (ost->frame_number >= ost->max_frames)
#endif
         return;

#if FF_API_LAVF_FMT_RAWPICTURE
      if (of->ctx->oformat->flags & AVFMT_RAWPICTURE &&
          enc->codec->id == AV_CODEC_ID_RAWVIDEO)
      {
         /* raw pictures are written as AVPicture structure to
           avoid any copies. We support temporarily the older
           method. */
         if (in_picture->interlaced_frame)
            mux_par->field_order = in_picture->top_field_first ? AV_FIELD_TB : AV_FIELD_BT;
         else
            mux_par->field_order = AV_FIELD_PROGRESSIVE;
         pkt.data = (uint8_t *)in_picture;
         pkt.size = sizeof(AVPicture);
         pkt.pts = av_rescale_q(in_picture->pts, enc->time_base, ost->st->time_base);
         pkt.flags |= AV_PKT_FLAG_KEY;

         output_packet(of, &pkt, ost);
      }
      else
#endif
      {
         int forced_keyframe = 0;
         double pts_time;

         if (enc->flags & (AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME) &&
             ost->top_field_first >= 0)
            in_picture->top_field_first = !!ost->top_field_first;

         if (in_picture->interlaced_frame)
         {
            if (enc->codec->id == AV_CODEC_ID_MJPEG)
               mux_par->field_order = in_picture->top_field_first ? AV_FIELD_TT : AV_FIELD_BB;
            else
               mux_par->field_order = in_picture->top_field_first ? AV_FIELD_TB : AV_FIELD_BT;
         }
         else
            mux_par->field_order = AV_FIELD_PROGRESSIVE;

         in_picture->quality = enc->global_quality;
         in_picture->pict_type = 0;

         pts_time = in_picture->pts != AV_NOPTS_VALUE ? in_picture->pts * av_q2d(enc->time_base) : NAN;
         if (ost->forced_kf_index < ost->forced_kf_count &&
             in_picture->pts >= ost->forced_kf_pts[ost->forced_kf_index])
         {
            ost->forced_kf_index++;
            forced_keyframe = 1;
         }
         else if (ost->forced_keyframes_pexpr)
         {
            double res;
            ost->forced_keyframes_expr_const_values[FKF_T] = pts_time;
            res = av_expr_eval(ost->forced_keyframes_pexpr,
                               ost->forced_keyframes_expr_const_values, NULL);
            ff_dlog(NULL, "force_key_frame: n:%f n_forced:%f prev_forced_n:%f t:%f prev_forced_t:%f -> res:%f\n",
                    ost->forced_keyframes_expr_const_values[FKF_N],
                    ost->forced_keyframes_expr_const_values[FKF_N_FORCED],
                    ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_N],
                    ost->forced_keyframes_expr_const_values[FKF_T],
                    ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_T],
                    res);
            if (res)
            {
               forced_keyframe = 1;
               ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_N] =
                   ost->forced_keyframes_expr_const_values[FKF_N];
               ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_T] =
                   ost->forced_keyframes_expr_const_values[FKF_T];
               ost->forced_keyframes_expr_const_values[FKF_N_FORCED] += 1;
            }

            ost->forced_keyframes_expr_const_values[FKF_N] += 1;
         }
         else if (ost->forced_keyframes && !strncmp(ost->forced_keyframes, "source", 6) && in_picture->key_frame == 1)
         {
            forced_keyframe = 1;
         }

         if (forced_keyframe)
         {
            in_picture->pict_type = AV_PICTURE_TYPE_I;
            av_log(NULL, AV_LOG_DEBUG, "Forced keyframe at time %f\n", pts_time);
         }

         if (debug_ts)
         {
            av_log(NULL, AV_LOG_INFO, "encoder <- type:video "
                                      "frame_pts:%s frame_pts_time:%s time_base:%d/%d\n",
                   av_ts2str(in_picture->pts), av_ts2timestr(in_picture->pts, &enc->time_base),
                   enc->time_base.num, enc->time_base.den);
         }

         ost->frames_encoded++;

         ret = avcodec_send_frame(enc, in_picture);
         if (ret < 0)
            goto error;

         while (1)
         {
            ret = avcodec_receive_packet(enc, &pkt);
            if (ret == AVERROR(EAGAIN))
               break;
            if (ret < 0)
               goto error;

            if (debug_ts)
            {
               av_log(NULL, AV_LOG_INFO, "encoder -> type:video "
                                         "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s\n",
                      av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &enc->time_base),
                      av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &enc->time_base));
            }

            if (pkt.pts == AV_NOPTS_VALUE && !(enc->codec->capabilities & AV_CODEC_CAP_DELAY))
               pkt.pts = ost->sync_opts;

            av_packet_rescale_ts(&pkt, enc->time_base, ost->st->time_base);

            if (debug_ts)
            {
               av_log(NULL, AV_LOG_INFO, "encoder -> type:video "
                                         "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s\n",
                      av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &ost->st->time_base),
                      av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &ost->st->time_base));
            }

            frame_size = pkt.size;
            output_packet(of, &pkt, ost);

            /* if two pass, output log */
            if (ost->logfile && enc->stats_out)
            {
               fprintf(ost->logfile, "%s", enc->stats_out);
            }
         }
      }
      ost->sync_opts++;
      /*
     * For video, number of frames in == number of packets out.
     * But there may be reordering, so we can't throw away frames on encoder
     * flush, we need to limit them here, before they go into encoder.
     */
      ost->frame_number++;

      if (vstats_filename && frame_size)
         do_video_stats(ost, frame_size);
   }

   if (!ost->last_frame)
      ost->last_frame = av_frame_alloc();
   av_frame_unref(ost->last_frame);
   if (next_picture && ost->last_frame)
      av_frame_ref(ost->last_frame, next_picture);
   else
      av_frame_free(&ost->last_frame);

   return;
error:
   av_log(NULL, AV_LOG_FATAL, "Video encoding failed\n");
   exit_program(1);
}

void do_audio_out(OutputFile *of, OutputStream *ost, AVFrame *frame)
{
   AVCodecContext *enc = ost->enc_ctx;
   AVPacket pkt;
   int ret;

   av_init_packet(&pkt);
   pkt.data = NULL;
   pkt.size = 0;

   if (!check_recording_time(ost))
      return;

   if (frame->pts == AV_NOPTS_VALUE || audio_sync_method < 0)
      frame->pts = ost->sync_opts;
   ost->sync_opts = frame->pts + frame->nb_samples;
   ost->samples_encoded += frame->nb_samples;
   ost->frames_encoded++;

   av_assert0(pkt.size || !pkt.data);
   if (debug_ts)
   {
      av_log(NULL, AV_LOG_INFO, "encoder <- type:audio "
                                "frame_pts:%s frame_pts_time:%s time_base:%d/%d\n",
             av_ts2str(frame->pts), av_ts2timestr(frame->pts, &enc->time_base),
             enc->time_base.num, enc->time_base.den);
   }

   ret = avcodec_send_frame(enc, frame);
   if (ret < 0)
      goto error;

   while (1)
   {
      ret = avcodec_receive_packet(enc, &pkt);
      if (ret == AVERROR(EAGAIN))
         break;
      if (ret < 0)
         goto error;

      av_packet_rescale_ts(&pkt, enc->time_base, ost->st->time_base);

      if (debug_ts)
      {
         av_log(NULL, AV_LOG_INFO, "encoder -> type:audio "
                                   "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s\n",
                av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &ost->st->time_base),
                av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &ost->st->time_base));
      }

      output_packet(of, &pkt, ost);
   }

   return;
error:
   av_log(NULL, AV_LOG_FATAL, "Audio encoding failed\n");
   exit_program(1);
}

void do_subtitle_out(OutputFile *of, OutputStream *ost, AVSubtitle *sub)
{
   int subtitle_out_max_size = 1024 * 1024;
   int subtitle_out_size, nb, i;
   AVCodecContext *enc;
   AVPacket pkt;
   int64_t pts;

   if (sub->pts == AV_NOPTS_VALUE)
   {
      av_log(NULL, AV_LOG_ERROR, "Subtitle packets must have a pts\n");
      if (exit_on_error)
         exit_program(1);
      return;
   }

   enc = ost->enc_ctx;

   if (!subtitle_out)
   {
      subtitle_out = av_malloc(subtitle_out_max_size);
      if (!subtitle_out)
      {
         av_log(NULL, AV_LOG_FATAL, "Failed to allocate subtitle_out\n");
         exit_program(1);
      }
   }

   /* Note: DVB subtitle need one packet to draw them and one other
       packet to clear them */
   /* XXX: signal it in the codec context ? */
   if (enc->codec_id == AV_CODEC_ID_DVB_SUBTITLE)
      nb = 2;
   else
      nb = 1;

   /* shift timestamp to honor -ss and make check_recording_time() work with -t */
   pts = sub->pts;
   if (output_files[ost->file_index]->start_time != AV_NOPTS_VALUE)
      pts -= output_files[ost->file_index]->start_time;
   for (i = 0; i < nb; i++)
   {
      unsigned save_num_rects = sub->num_rects;

      ost->sync_opts = av_rescale_q(pts, AV_TIME_BASE_Q, enc->time_base);
      if (!check_recording_time(ost))
         return;

      sub->pts = pts;
      // start_display_time is required to be 0
      sub->pts += av_rescale_q(sub->start_display_time, (AVRational){1, 1000}, AV_TIME_BASE_Q);
      sub->end_display_time -= sub->start_display_time;
      sub->start_display_time = 0;
      if (i == 1)
         sub->num_rects = 0;

      ost->frames_encoded++;

      subtitle_out_size = avcodec_encode_subtitle(enc, subtitle_out,
                                                  subtitle_out_max_size, sub);
      if (i == 1)
         sub->num_rects = save_num_rects;
      if (subtitle_out_size < 0)
      {
         av_log(NULL, AV_LOG_FATAL, "Subtitle encoding failed\n");
         exit_program(1);
      }

      av_init_packet(&pkt);
      pkt.data = subtitle_out;
      pkt.size = subtitle_out_size;
      pkt.pts = av_rescale_q(sub->pts, AV_TIME_BASE_Q, ost->st->time_base);
      pkt.duration = av_rescale_q(sub->end_display_time, (AVRational){1, 1000}, ost->st->time_base);
      if (enc->codec_id == AV_CODEC_ID_DVB_SUBTITLE)
      {
         /* XXX: the pts correction is handled here. Maybe handling
               it in the codec would be better */
         if (i == 0)
            pkt.pts += 90 * sub->start_display_time;
         else
            pkt.pts += 90 * sub->end_display_time;
      }
      pkt.dts = pkt.pts;
      output_packet(of, &pkt, ost);
   }
}

/////////////////////////

void OutputFile::finish_if_shortest()
{
   if (shortest)
   {
      for (auto ost = streams.begin(); ost < streams.end(); ost++)
         ost->finished = ENCODER_FINISHED | MUXER_FINISHED;
   }
}

////////////////////////////

OutputStream new_video_stream(OptionsContext *o, AVFormatContext *oc, InputStream *src)
{
   AVStream *st;
   AVCodecContext *video_enc;
   char *frame_rate = NULL, *frame_aspect_ratio = NULL;

   // create new output stream for the latest output file & add the stream reference to the output_streams
   output_streams.push_back(output_files.back().new_stream(o, oc, AVMEDIA_TYPE_VIDEO, src));
   OutputStream &ost = output_streams.back();

   st = ost->st;
   video_enc = ost->enc_ctx;

   MATCH_PER_STREAM_OPT(frame_rates, str, frame_rate, oc, st);
   if (frame_rate && av_parse_video_rate(&ost->frame_rate, frame_rate) < 0)
   {
      av_log(NULL, AV_LOG_FATAL, "Invalid framerate value: %s\n", frame_rate);
      exit_program(1);
   }
   if (frame_rate && video_sync_method == VSYNC_PASSTHROUGH)
      av_log(NULL, AV_LOG_ERROR, "Using -vsync 0 and -r can produce invalid output files\n");

   MATCH_PER_STREAM_OPT(frame_aspect_ratios, str, frame_aspect_ratio, oc, st);
   if (frame_aspect_ratio)
   {
      AVRational q;
      if (av_parse_ratio(&q, frame_aspect_ratio, 255, 0, NULL) < 0 ||
          q.num <= 0 || q.den <= 0)
      {
         av_log(NULL, AV_LOG_FATAL, "Invalid aspect ratio: %s\n", frame_aspect_ratio);
         exit_program(1);
      }
      ost->frame_aspect_ratio = q;
   }

   MATCH_PER_STREAM_OPT(filter_scripts, str, ost->filters_script, oc, st);
   MATCH_PER_STREAM_OPT(filters, str, ost->filters, oc, st);

   if (!ost->stream_copy)
   {
      const char *p = NULL;
      char *frame_size = NULL;
      char *frame_pix_fmt = NULL;
      char *intra_matrix = NULL, *inter_matrix = NULL;
      char *chroma_intra_matrix = NULL;
      int do_pass = 0;
      int i;

      MATCH_PER_STREAM_OPT(frame_sizes, str, frame_size, oc, st);
      if (frame_size && av_parse_video_size(&video_enc->width, &video_enc->height, frame_size) < 0)
      {
         av_log(NULL, AV_LOG_FATAL, "Invalid frame size: %s.\n", frame_size);
         exit_program(1);
      }

      video_enc->bits_per_raw_sample = frame_bits_per_raw_sample;
      MATCH_PER_STREAM_OPT(frame_pix_fmts, str, frame_pix_fmt, oc, st);
      if (frame_pix_fmt && *frame_pix_fmt == '+')
      {
         ost->keep_pix_fmt = 1;
         if (!*++frame_pix_fmt)
            frame_pix_fmt = NULL;
      }
      if (frame_pix_fmt && (video_enc->pix_fmt = av_get_pix_fmt(frame_pix_fmt)) == AV_PIX_FMT_NONE)
      {
         av_log(NULL, AV_LOG_FATAL, "Unknown pixel format requested: %s.\n", frame_pix_fmt);
         exit_program(1);
      }
      st->sample_aspect_ratio = video_enc->sample_aspect_ratio;

      if (intra_only)
         video_enc->gop_size = 0;
      MATCH_PER_STREAM_OPT(intra_matrices, str, intra_matrix, oc, st);
      if (intra_matrix)
      {
         if (!(video_enc->intra_matrix = av_mallocz(sizeof(*video_enc->intra_matrix) * 64)))
         {
            av_log(NULL, AV_LOG_FATAL, "Could not allocate memory for intra matrix.\n");
            exit_program(1);
         }
         parse_matrix_coeffs(video_enc->intra_matrix, intra_matrix);
      }
      MATCH_PER_STREAM_OPT(chroma_intra_matrices, str, chroma_intra_matrix, oc, st);
      if (chroma_intra_matrix)
      {
         uint16_t *p = av_mallocz(sizeof(*video_enc->chroma_intra_matrix) * 64);
         if (!p)
         {
            av_log(NULL, AV_LOG_FATAL, "Could not allocate memory for intra matrix.\n");
            exit_program(1);
         }
         av_codec_set_chroma_intra_matrix(video_enc, p);
         parse_matrix_coeffs(p, chroma_intra_matrix);
      }
      MATCH_PER_STREAM_OPT(inter_matrices, str, inter_matrix, oc, st);
      if (inter_matrix)
      {
         if (!(video_enc->inter_matrix = av_mallocz(sizeof(*video_enc->inter_matrix) * 64)))
         {
            av_log(NULL, AV_LOG_FATAL, "Could not allocate memory for inter matrix.\n");
            exit_program(1);
         }
         parse_matrix_coeffs(video_enc->inter_matrix, inter_matrix);
      }

      MATCH_PER_STREAM_OPT(rc_overrides, str, p, oc, st);
      for (i = 0; p; i++)
      {
         int start, end, q;
         int e = sscanf(p, "%d,%d,%d", &start, &end, &q);
         if (e != 3)
         {
            av_log(NULL, AV_LOG_FATAL, "error parsing rc_override\n");
            exit_program(1);
         }
         video_enc->rc_override =
             av_realloc_array(video_enc->rc_override,
                              i + 1, sizeof(RcOverride));
         if (!video_enc->rc_override)
         {
            av_log(NULL, AV_LOG_FATAL, "Could not (re)allocate memory for rc_override.\n");
            exit_program(1);
         }
         video_enc->rc_override[i].start_frame = start;
         video_enc->rc_override[i].end_frame = end;
         if (q > 0)
         {
            video_enc->rc_override[i].qscale = q;
            video_enc->rc_override[i].quality_factor = 1.0;
         }
         else
         {
            video_enc->rc_override[i].qscale = 0;
            video_enc->rc_override[i].quality_factor = -q / 100.0;
         }
         p = strchr(p, '/');
         if (p)
            p++;
      }
      video_enc->rc_override_count = i;

      if (do_psnr)
         video_enc->flags |= AV_CODEC_FLAG_PSNR;

      /* two pass mode */
      MATCH_PER_STREAM_OPT(pass, i, do_pass, oc, st);
      if (do_pass)
      {
         if (do_pass & 1)
         {
            video_enc->flags |= AV_CODEC_FLAG_PASS1;
            av_dict_set(&ost->encoder_opts, "flags", "+pass1", AV_DICT_APPEND);
         }
         if (do_pass & 2)
         {
            video_enc->flags |= AV_CODEC_FLAG_PASS2;
            av_dict_set(&ost->encoder_opts, "flags", "+pass2", AV_DICT_APPEND);
         }
      }

      MATCH_PER_STREAM_OPT(passlogfiles, str, ost->logfile_prefix, oc, st);
      if (ost->logfile_prefix &&
          !(ost->logfile_prefix = av_strdup(ost->logfile_prefix)))
         exit_program(1);

      if (do_pass)
      {
         char logfilename[1024];
         FILE *f;

         snprintf(logfilename, sizeof(logfilename), "%s-%d.log",
                  ost->logfile_prefix ? ost->logfile_prefix : DEFAULT_PASS_LOGFILENAME_PREFIX,
                  i);
         if (!strcmp(ost->enc->name, "libx264"))
         {
            av_dict_set(&ost->encoder_opts, "stats", logfilename, AV_DICT_DONT_OVERWRITE);
         }
         else
         {
            if (video_enc->flags & AV_CODEC_FLAG_PASS2)
            {
               char *logbuffer = read_file(logfilename);

               if (!logbuffer)
               {
                  av_log(NULL, AV_LOG_FATAL, "Error reading log file '%s' for pass-2 encoding\n",
                         logfilename);
                  exit_program(1);
               }
               video_enc->stats_in = logbuffer;
            }
            if (video_enc->flags & AV_CODEC_FLAG_PASS1)
            {
               f = av_fopen_utf8(logfilename, "wb");
               if (!f)
               {
                  av_log(NULL, AV_LOG_FATAL,
                         "Cannot write log file '%s' for pass-1 encoding: %s\n",
                         logfilename, strerror(errno));
                  exit_program(1);
               }
               ost->logfile = f;
            }
         }
      }

      MATCH_PER_STREAM_OPT(forced_key_frames, str, ost->forced_keyframes, oc, st);
      if (ost->forced_keyframes)
         ost->forced_keyframes = av_strdup(ost->forced_keyframes);

      MATCH_PER_STREAM_OPT(force_fps, i, ost->force_fps, oc, st);

      ost->top_field_first = -1;
      MATCH_PER_STREAM_OPT(top_field_first, i, ost->top_field_first, oc, st);

      ost->avfilter = get_ost_filters(o, oc, ost);
      if (!ost->avfilter)
         exit_program(1);
   }
   else
   {
      MATCH_PER_STREAM_OPT(copy_initial_nonkeyframes, i, ost->copy_initial_nonkeyframes, oc, st);
   }

   if (ost->stream_copy)
      check_streamcopy_filters(o, oc, ost, AVMEDIA_TYPE_VIDEO);

   return ost;
}

OutputStream new_audio_stream(OptionsContext *o, AVFormatContext *oc, int source_index)
{
   int n;
   AVStream *st;
   AVCodecContext *audio_enc;

   // create new output stream for the latest output file & add the stream reference to the output_streams
   output_streams.push_back(output_files.back().new_stream(o, oc, AVMEDIA_TYPE_AUDIO, src));
   OutputStream &ost = output_streams.back();

   st = ost.st;

   audio_enc = ost->enc_ctx;
   audio_enc->codec_type = AVMEDIA_TYPE_AUDIO;

   MATCH_PER_STREAM_OPT(filter_scripts, str, ost->filters_script, oc, st);
   MATCH_PER_STREAM_OPT(filters, str, ost->filters, oc, st);

   if (!ost->stream_copy)
   {
      char *sample_fmt = NULL;

      MATCH_PER_STREAM_OPT(audio_channels, i, audio_enc->channels, oc, st);

      MATCH_PER_STREAM_OPT(sample_fmts, str, sample_fmt, oc, st);
      if (sample_fmt &&
          (audio_enc->sample_fmt = av_get_sample_fmt(sample_fmt)) == AV_SAMPLE_FMT_NONE)
      {
         av_log(NULL, AV_LOG_FATAL, "Invalid sample format '%s'\n", sample_fmt);
         exit_program(1);
      }

      MATCH_PER_STREAM_OPT(audio_sample_rate, i, audio_enc->sample_rate, oc, st);

      MATCH_PER_STREAM_OPT(apad, str, ost->apad, oc, st);
      ost->apad = av_strdup(ost->apad);

      ost->avfilter = get_ost_filters(o, oc, ost);
      if (!ost->avfilter)
         exit_program(1);

      /* check for channel mapping for this audio stream */
      for (n = 0; n < o.nb_audio_channel_maps; n++)
      {
         AudioChannelMap *map = &o.audio_channel_maps[n];
         if ((map->ofile_idx == -1 || ost->file_index == map->ofile_idx) &&
             (map->ostream_idx == -1 || ost->st->index == map->ostream_idx))
         {
            InputStream *ist;

            if (map->channel_idx == -1)
            {
               ist = NULL;
            }
            else if (ost->source_index < 0)
            {
               av_log(NULL, AV_LOG_FATAL, "Cannot determine input stream for channel mapping %d.%d\n",
                      ost->file_index, ost->st->index);
               continue;
            }
            else
            {
               ist = input_streams[ost->source_index];
            }

            if (!ist || (ist->file_index == map->file_idx && ist->st->index == map->stream_idx))
               ost->audio_channels_map.push_back(map->channel_idx);
         }
      }
   }

   if (ost->stream_copy)
      check_streamcopy_filters(o, oc, ost, AVMEDIA_TYPE_AUDIO);

   return ost;
}

OutputStream new_data_stream(OptionsContext *o, AVFormatContext *oc, int source_index)
{
   // create new output stream for the latest output file & add the stream reference to the output_streams
   output_streams.push_back(output_files.back().new_stream(o, oc, AVMEDIA_TYPE_DATA, src));
   OutputStream &ost = output_streams.back();

   if (!ost->stream_copy)
   {
      av_log(NULL, AV_LOG_FATAL, "Data stream encoding not supported yet (only streamcopy)\n");
      exit_program(1);
   }

   return ost;
}

OutputStream new_unknown_stream(OptionsContext *o, AVFormatContext *oc, int source_index)
{
   // create new output stream for the latest output file & add the stream reference to the output_streams
   output_streams.push_back(output_files.back().new_stream(o, oc, AVMEDIA_TYPE_UNKNOWN, src));
   OutputStream &ost = output_streams.back();
   if (!ost.stream_copy)
   {
      av_log(NULL, AV_LOG_FATAL, "Unknown stream encoding not supported yet (only streamcopy)\n");
      exit_program(1);
   }

   return ost;
}

OutputStream new_attachment_stream(OptionsContext *o, AVFormatContext *oc, int source_index)
{
   // create new output stream for the latest output file & add the stream reference to the output_streams
   output_streams.push_back(output_files.back().new_stream(o, oc, AVMEDIA_TYPE_ATTACHMENT, src));
   OutputStream &ost = output_streams.back();
   ost.stream_copy = 1;
   ost.finished = 1;
   return ost;
}

OutputStream new_subtitle_stream(OptionsContext *o, AVFormatContext *oc, int source_index)
{
   AVStream *st;
   OutputStream *ost;
   AVCodecContext *subtitle_enc;

   // create new output stream for the latest output file & add the stream reference to the output_streams
   output_streams.push_back(output_files.back().new_stream(o, oc, AVMEDIA_TYPE_SUBTITLE, src));
   OutputStream &ost = output_streams.back();
   st = ost.st;
   subtitle_enc = ost->enc_ctx;

   subtitle_enc->codec_type = AVMEDIA_TYPE_SUBTITLE;

   MATCH_PER_STREAM_OPT(copy_initial_nonkeyframes, i, ost->copy_initial_nonkeyframes, oc, st);

   if (!ost->stream_copy)
   {
      char *frame_size = NULL;

      MATCH_PER_STREAM_OPT(frame_sizes, str, frame_size, oc, st);
      if (frame_size && av_parse_video_size(&subtitle_enc->width, &subtitle_enc->height, frame_size) < 0)
      {
         av_log(NULL, AV_LOG_FATAL, "Invalid frame size: %s.\n", frame_size);
         exit_program(1);
      }
   }

   return ost;
}

