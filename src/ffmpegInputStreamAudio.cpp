#include "ffmpegInputStream.h"

AudioInputStream::AudioInputStream(InputFile &f, const int i, const InputOptionsContext &o)
    : InputStream(f, i, o), guess_layout_max(INT_MAX), resample_sample_fmt(dec_ctx->sample_fmt), resample_sample_rate(dec_ctx->sample_rate),
      resample_channels(dec_ctx->channels), resample_channel_layout(dec_ctx->channel_layout)
{
   const int *opt_int;
   if (opt_int = o.getspec<SpecifierOptsInt, int>("guess_layout_max", file.ctx.get(), st))
      guess_layout_max = *opt_int;

   guess_input_channel_layout();

   // update the parameter
   if (avcodec_parameters_from_context(st->codecpar, dec_ctx.get()) < 0)
      throw ffmpegException("Error initializing the decoder context.");
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool AudioInputStream::guess_input_channel_layout()
{
   // if channel layout is not given, make a guess
   if (!dec_ctx->channel_layout)
   {
      if (dec_ctx->channels > guess_layout_max)
         return false;
      dec_ctx->channel_layout = av_get_default_channel_layout(dec_ctx->channels);
      if (!dec_ctx->channel_layout)
         return false;

      // std::string layout_name;
      // layout_name.reserve(256);
      // av_get_channel_layout_string(layout_name.data(), layout_name.size(), dec_ctx->channels, dec_ctx->channel_layout);
      // av_log(NULL, AV_LOG_WARNING, "Guessed Channel Layout for Input Stream " "#%d.%d : %s\n",
      //        ist->file_index, ist->st->index, layout_name);
   }
   return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

//int decode_audio(InputStream *ist, AVPacket *pkt, int *got_output)
int AudioInputStream::decode_packet(const AVPacket *inpkt, bool repeating, bool &got_output)

{
   AVPacket avpkt;
   if (!inpkt)
   {
      /* EOF handling */
      av_init_packet(&avpkt);
      avpkt.data = NULL;
      avpkt.size = 0;
   }
   else
   {
      avpkt = *inpkt;
   }


   int ret;
   AVPacket *pkt = repeating ? NULL : &avpkt;

   // allocate frame buffers
   if (!decoded_frame && !(decoded_frame = av_frame_alloc()))
      return AVERROR(ENOMEM);
   if (!filter_frame && !(filter_frame = av_frame_alloc()))
      return AVERROR(ENOMEM);

   ret = decode(dec_ctx, decoded_frame, got_output, pkt);

   if (ret >= 0 && dec_ctx->sample_rate <= 0)
   {
      av_log(dec_ctx, AV_LOG_ERROR, "Sample rate %d invalid\n", dec_ctx->sample_rate);
      ret = AVERROR_INVALIDDATA;
   }

   if (ret != AVERROR_EOF)
      check_decode_result(ist, got_output, ret);

   if (!got_output || ret < 0)
      return ret;

   // update the decoding statistics
   samples_decoded += decoded_frame->nb_samples;
   frames_decoded++;

#if 1
   /* increment next_dts to use for the case where the input stream does not
       have timestamps or there are multiple frames in the packet */
   next_pts += ((int64_t)AV_TIME_BASE * decoded_frame->nb_samples) / dec_ctx->sample_rate;
   next_dts += ((int64_t)AV_TIME_BASE * decoded_frame->nb_samples) / dec_ctx->sample_rate;
#endif

   // if resampling is required
   if (resample_sample_fmt != decoded_frame->format ||
       resample_channels != dec_ctx->channels ||
       resample_channel_layout != decoded_frame->channel_layout ||
       resample_sample_rate != decoded_frame->sample_rate)
   {
      if (!guess_input_channel_layout())
      {
         std::ostringstream msg;
         msg << "Unable to find default channel layout for Input Stream #" << file.idnex << "." << st->index;
         throw ffmpegException(msg.str());
      }
      decoded_frame->channel_layout = dec_ctx->channel_layout;

      // std::string layout1,layout2;
      // layout1.reserve(64);
      // layout2.reserve(64);
      // av_get_channel_layout_string(layout1.data(), layout1.size()), resample_channels,resample_channel_layout);
      // av_get_channel_layout_string(layout2.data(), layout2.size()), dec_ctx->channels, decoded_frame->channel_layout);
      // av_log(NULL, AV_LOG_INFO,
      //        "Input stream #%d:%d frame changed from rate:%d fmt:%s ch:%d chl:%s to rate:%d fmt:%s ch:%d chl:%s\n",
      //        file_index, st->index,
      //        resample_sample_rate, av_get_sample_fmt_name(resample_sample_fmt),
      //        resample_channels, layout1,
      //        decoded_frame->sample_rate, av_get_sample_fmt_name(decoded_frame->format),
      //        dec_ctx->channels, layout2);

      resample_sample_fmt = decoded_frame->format;
      resample_sample_rate = decoded_frame->sample_rate;
      resample_channel_layout = decoded_frame->channel_layout;
      resample_channels = dec_ctx->channels;

      for (int i = 0; i < nb_filtergraphs; i++)
         if (ist_in_filtergraph(filtergraphs[i], this))
         {
            FilterGraph *fg = filtergraphs[i];
            if (configure_filtergraph(fg) < 0)
               throw ffmpegException("Error reinitializing filters!");
         }
   }

   // update timing
   AVRational decoded_frame_tb;
   if (decoded_frame->pts != AV_NOPTS_VALUE)
   {
      decoded_frame_tb = st->time_base;
   }
   else if (pkt && pkt->pts != AV_NOPTS_VALUE)
   {
      decoded_frame->pts = pkt->pts;
      decoded_frame_tb = time_base;
   }
   else
   {
      decoded_frame->pts = dts;
      decoded_frame_tb = AV_TIME_BASE_Q;
   }
   if (decoded_frame->pts != AV_NOPTS_VALUE)
      decoded_frame->pts = av_rescale_delta(decoded_frame_tb, decoded_frame->pts,
                                            (AVRational){1, dec_ctx->sample_rate}, decoded_frame->nb_samples, &filter_in_rescale_delta_last,
                                            (AVRational){1, dec_ctx->sample_rate});

   nb_samples = decoded_frame->nb_samples;

   int err = 0;
   for (int i = 0; i < nb_filters; i++)
   {
      AVFrame *f;
      if (i < nb_filters - 1)
      {
         f = filter_frame;
         err = av_frame_ref(f, decoded_frame);
         if (err < 0)
            break;
      }
      else
         f = decoded_frame;
      err = av_buffersrc_add_frame_flags(filters[i]->filter, f, AV_BUFFERSRC_FLAG_PUSH);
      if (err == AVERROR_EOF)
         err = 0; /* ignore */
      if (err < 0)
         break;
   }
   decoded_frame->pts = AV_NOPTS_VALUE;

   av_frame_unref(filter_frame);
   av_frame_unref(decoded_frame);
   return err < 0 ? err : ret;
}

/* pkt = NULL means EOF (needed to flush decoder buffers) */
int AudioInputStream::prepare_packet(const AVPacket *pkt, bool no_eof)
{
   int ret = InputStream::prepare_packet(pkt, no_eof);

   if (!decoding_needed) // if copying set next_dts & next_pts per stream info
   {
      next_dts += ((int64_t)AV_TIME_BASE * dec_ctx->frame_size) / dec_ctx->sample_rate;
      next_pts = next_dts;
   }

   return ret;
}
