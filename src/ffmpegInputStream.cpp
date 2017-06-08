#include "ffmpegInputStream.h"

#include <algorithm> // std::fill
#include <utility>   // std::swap
#include <cmath>     // std::floor, std::fabs, std::round
#include <cstring>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/error.h>
}

#include "ffmpegInputFile.h"
#include "ffmpegUtil.h"

using namespace ffmpeg;

int64_t InputStream::decode_error_stat[2] = {0, 0};

// av_frame_free(&ist->decoded_frame);
// av_frame_free(&ist->filter_frame);
// avsubtitle_free(&ist->prev_sub.subtitle);
// av_frame_free(&ist->sub2video.frame);
// av_freep(&ist->filters);
// av_freep(&ist->hwaccel_device);
// av_freep(&ist->dts_buffer);

AVPixelFormat InputStream::get_format(AVCodecContext *s, const AVPixelFormat *pix_fmts)
{
   InputStream *ist = (InputStream *)s->opaque;
   const enum AVPixelFormat *p;

   for (p = pix_fmts; *p != -1; p++)
   {
      const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);

      // if not hardware accelerated VideoInputStream, done
      if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
         break;

      bool unknown; // only relevant if get_hwaccel_format returns true
      if (ist->get_hwaccel_format(p, unknown))
      {
         if (!unknown)
            return AV_PIX_FMT_NONE;
         break;
      }
   }

   return *p;
}

int InputStream::get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
   return ((InputStream *)s->opaque)->get_stream_buffer(s, frame, flags);
}

int InputStream::get_stream_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
   return avcodec_default_get_buffer2(s, frame, flags);
}

InputStream::InputStream(InputFile &f, const int i, const InputOptionsContext &o)
    : file(f), dec(NULL), dec_ctx(NULL, delete_codec_ctx), decoded_frame(NULL), filter_frame(NULL), st(f.ctx->streams[i]), 
      decoder_opts(NULL, delete_dict), discard(true), user_set_discard(AVDISCARD_NONE),
      nb_samples(0), min_pts(INT64_MAX), max_pts(INT64_MIN), ts_scale(1.0)
{
   std::ostringstream msg; // for exception message
   const Option *opt = NULL;

   // grab the option from OptionsContext
   if (opt = o.cfind("itsscale"))
      ts_scale = ((OptionDouble *)opt)->value;

   dec = o.choose_decoder(file.ctx.get(), st);
   decoder_opts.reset(o.filter_codec_opts(st->codecpar->codec_id, file.ctx.get(), st, dec));

   // const std::string *discard_str = NULL;
   // if ((opt = o.find("discard")) &&
   //     (discard_str = ((SpecifierOptsString *)opt)->tryget(file.ctx, st)))
   if (const std::string *discard_str = o.getspec<SpecifierOptsString, std::string>("discard", file.ctx.get(), st))
   {
      const AVClass *cc = avcodec_get_class();
      const AVOption *discard_opt = av_opt_find(&cc, "skip_frame", NULL, 0, 0);
      if (av_opt_eval_int(&cc, discard_opt, discard_str->c_str(), (int *)&user_set_discard) < 0)
         throw ffmpegException("Error parsing discard " + *discard_str + ".");
   }

   // create decoder context
   dec_ctx.reset(avcodec_alloc_context3(dec));
   if (!dec_ctx)
      throw ffmpegException("Error allocating the decoder context.");

   // set the stream parameters to the decoder context
   if (avcodec_parameters_to_context(dec_ctx.get(), st->codecpar) < 0)
      throw ffmpegException("Error initializing the decoder context.");

   // FILTER RELATED VARIABLES, REVISIT LATER
   // ist->reinit_filters = -1;
   // MATCH_PER_STREAM_OPT(reinit_filters, i, ist->reinit_filters, ic, st);
   // ist->filter_in_rescale_delta_last = AV_NOPTS_VALUE;
}

// InputStream::~InputStream()
// {
//    av_frame_free(&decoded_frame);
//    av_frame_free(&filter_frame);
//    avsubtitle_free(&prev_sub.subtitle);
//    av_frame_free(&sub2video.frame);
//    // av_freep(&hwaccel_device);
//    av_freep(&dts_buffer);

//    avcodec_free_context(&dec_ctx);
// }

void InputStream::remove_used_opts(AVDictionary *&opts)
{
   AVDictionaryEntry *e = NULL;
   AVDictionary *dopts = decoder_opts.get();
   while ((e = av_dict_get(opts, "", e, AV_DICT_IGNORE_SUFFIX)))
      av_dict_set(&dopts, e->key, NULL, 0);
   if (!decoder_opts.get())
      decoder_opts.reset(opts);
}
///////////////////////////////////////////////////////////////////////////////////////////////////

DataInputStream::DataInputStream(InputFile &f, const int i, const InputOptionsContext &o)
    : InputStream(f, i, o), fix_sub_duration(0)
{
   const int *opt_int;
   const std::string *opt_str;

   // if decoder has not been forced, set it automatically
   if (!dec)
      dec = avcodec_find_decoder(st->codecpar->codec_id);

   if (opt_int = o.getspec<SpecifierOptsInt, int>("fix_sub_duration", file.ctx.get(), st))
      fix_sub_duration = *opt_int;

   if ((opt_str = o.getspec<SpecifierOptsString, std::string>("canvas_size", file.ctx.get(), st)) &&
       (av_parse_video_size(&dec_ctx->width, &dec_ctx->height, opt_str->c_str()) < 0))
      throw ffmpegException("Invalid canvas size: " + *opt_str + ".");

   // update the parameter
   if (avcodec_parameters_from_context(st->codecpar, dec_ctx.get()) < 0)
      throw ffmpegException("Error initializing the decoder context.");
}

///////////////////////////////////////////////////////////////////////////////////////////////////

int InputStream::init_stream(std::string &error)
{
   int ret;
   AVDictionary *opts = decoder_opts.get();

   saw_first_ts = false; // guarantees to set dts & pts in process_packet

   if (decoding_needed)
   {
      if (!dec)
      {
         std::ostringstream msg;
         msg << "Decoder (codec " << std::string(avcodec_get_name(dec_ctx->codec_id)) << ") not found for input stream #" << file.index << ":" << st->index;
         error = msg.str();
         return AVERROR(EINVAL);
      }

      dec_ctx->opaque = this;
      dec_ctx->get_format = InputStream::get_format;
      dec_ctx->get_buffer2 = InputStream::get_buffer;
      dec_ctx->thread_safe_callbacks = 1;

      av_opt_set_int(dec_ctx.get(), "refcounted_frames", 1, 0);
      if (dec_ctx->codec_id == AV_CODEC_ID_DVB_SUBTITLE && (decoding_needed & DECODING_FOR_OST))
      {
         av_dict_set(&opts, "compute_edt", "1", AV_DICT_DONT_OVERWRITE);
         // if (decoding_needed & DECODING_FOR_FILTER)
         //    av_log(NULL, AV_LOG_WARNING, "Warning using DVB subtitles for filtering and output at the same time is not fully supported, also see -compute_edt [0|1]\n");
      }

      av_dict_set(&opts, "sub_text_format", "ass", AV_DICT_DONT_OVERWRITE);

      /* Useful for subtitles retiming by lavf (FIXME), skipping samples in
         * audio, and video decoders such as cuvid or mediacodec */
      av_codec_set_pkt_timebase(dec_ctx.get(), st->time_base);

      if (!av_dict_get(opts, "threads", NULL, 0))
         av_dict_set(&opts, "threads", "auto", 0);
      if ((ret = avcodec_open2(dec_ctx.get(), dec, &opts)) < 0)
      {
         error.reserve(AV_ERROR_MAX_STRING_SIZE);
         av_make_error_string(error.data(), AV_ERROR_MAX_STRING_SIZE, ret);
         std::ostringstream msg;
         msg << "Error while opening decoder for input stream #" << file.index << ":" << st->index << " : " << error.c_str();
         error = msg.str();
         if (ret == AVERROR_EXPERIMENTAL)
            throw ffmpegException(error);
         return ret;
      }
      assert_avoptions(opts);
   }

   next_pts = AV_NOPTS_VALUE;
   next_dts = AV_NOPTS_VALUE;

   return 0;
}

/* pkt = NULL means EOF (needed to flush decoder buffers) */
int InputStream::prepare_packet(const AVPacket *pkt, bool no_eof)
{
   int ret = 0, i;
   bool repeating = false;
   bool eof_reached = false;

   if (!saw_first_ts)
   {
      dts = st->avg_frame_rate.num ? int64_t(-dec_ctx->has_b_frames * AV_TIME_BASE / av_q2d(st->avg_frame_rate)) : 0;
      pts = 0;
      if (pkt && pkt->pts != AV_NOPTS_VALUE && !decoding_needed)
      {
         dts += av_rescale_q(pkt->pts, st->time_base, AV_TIME_BASE_Q);
         pts = dts; //unused but better to set it to a value thats not totally wrong
      }
      saw_first_ts = true;
   }

   if (next_dts == AV_NOPTS_VALUE)
      next_dts = dts;
   if (next_pts == AV_NOPTS_VALUE)
      next_pts = pts;

   if (pkt && pkt->dts != AV_NOPTS_VALUE)
   {
      next_dts = dts = av_rescale_q(pkt->dts, st->time_base, AV_TIME_BASE_Q);
      if (dec_ctx->codec_type != AVMEDIA_TYPE_VIDEO || !decoding_needed)
         next_pts = pts = dts;
   }

   if (decoding_needed) // decode packet to frame
   {
      // while we have more to decode or while the decoder did output something on EOF
      while (true)
      {
         int duration = 0;
         bool got_output = false;

         pts = next_pts;
         dts = next_dts;

         ret = decode_packet(pkt, repeating, got_output);
         if (ret == AVERROR_EOF)
         {
            eof_reached = true;
            break;
         }

         if (ret < 0)
         {
            // Decoding might not terminate if we're draining the decoder, and
            // the decoder keeps returning an error.
            // This should probably be considered a libavcodec issue.
            // Sample: fate-vsynth1-dnxhd-720p-hr-lb
            if (!pkt)
               eof_reached = true;

            std::ostringstream msg;
            msg << "Error while decoding stream #" << file.index << ":" << st->index << ": " << av_err2str(ret);
            throw ffmpegException(msg.str());
         }

         if (!got_output)
            break;

         // During draining, we might get multiple output frames in this loop.
         // ffmpeg.c does not drain the filter chain on configuration changes,
         // which means if we send multiple frames at once to the filters, and
         // one of those frames changes configuration, the buffered frames will
         // be lost. This can upset certain FATE tests.
         // Decode only 1 frame per call on EOF to appease these FATE tests.
         // The ideal solution would be to rewrite decoding to use the new
         // decoding API in a better way.
         if (!pkt)
            break;

         repeating = true;
      }

      /* after flushing, send an EOF on all the filter inputs attached to the stream */
      /* except when looping we need to flush but not to send an EOF */
      if (!pkt && eof_reached && !no_eof && (send_filter_eof() < 0))
         throw ffmpegException("Error marking filters as finished");
   }
   else // leave the packet alone (e.g., to be copied) /* handle stream copy */
   {
      dts = next_dts;
      pts = dts;
      next_pts = next_dts;
   }

   return !eof_reached;
}

int InputStream::send_filter_eof()
{
   for (auto f = filters.begin(); f < filters.end(); f++)
   {
      int ret = av_buffersrc_add_frame(f->filter, NULL);
      if (ret < 0)
         return ret;
   }
   return 0;
}

// This does not quite work like avcodec_decode_audio4/avcodec_decode_video2.
// There is the following difference: if you got a frame, you must call
// it again with pkt=NULL. pkt==NULL is treated differently from pkt.size==0
// (pkt==NULL means get more output, pkt.size==0 is a flush/drain packet)
int InputStream::decode(AVCodecContext *avctx, AVFrame *frame, bool &got_frame, AVPacket *pkt)
{
   int ret;

   got_frame = false;

   if (pkt)
   {
      ret = avcodec_send_packet(avctx, pkt);
      // In particular, we don't expect AVERROR(EAGAIN), because we read all
      // decoded frames with avcodec_receive_frame() until done.
      if (ret < 0 && ret != AVERROR_EOF)
         return ret;
   }

   ret = avcodec_receive_frame(avctx, frame);
   if (ret < 0 && ret != AVERROR(EAGAIN))
      return ret;
   got_frame = (ret >= 0);

   return 0;
}

void InputStream::check_decode_result(const bool got_output, const int ret)
{
   if (got_output || ret < 0)
      decode_error_stat[ret < 0]++;

   if (ret < 0 && exit_on_error)
      throw ffmpegException("Decoding a packet failed.");

   if (got_output && dec_ctx->codec_type != AVMEDIA_TYPE_SUBTITLE)
   {
      if (av_frame_get_decode_error_flags(decoded_frame) || (decoded_frame->flags & AV_FRAME_FLAG_CORRUPT))
      {
         std::ostringstream msg;
         msg << file.ctx->filename << ": corrupt decoded frame in stream " << st->index;
         throw ffmpegException(msg.str());
      }
   }
}

void InputStream::close()
{
   if (decoding_needed)
   {
      avcodec_close(dec_ctx);
      if (hwaccel_uninit)
         hwaccel_uninit(ist->dec_ctx);
   }
}

bool InputStream::process_packet_time(AVPacket &pkt, int64_t &ts_offset, int64_t &last_ts)
{

   data_size += pkt.size;
   nb_packets++;

   // if this stream is not used, exit
   if (discard)
      return false;

   // make sure the packet is free of corruption
   if (pkt.flags & AV_PKT_FLAG_CORRUPT)
   {
      std::ostringstream msg;
      msg << file.ctx->filename << ": corrupt input packet in stream " << pkt.stream_index;
      throw ffmpegException(msg.str());
   }

   if (!wrap_correction_done && file.ctx->start_time != AV_NOPTS_VALUE && st->pts_wrap_bits < 64)
   {
      int64_t stime, stime2;
      // Correcting starttime based on the enabled streams
      // FIXME this ideally should be done before the first use of starttime but we do not know which are the enabled streams at that point.
      //       so we instead do it here as part of discontinuity handling
      if (next_dts == AV_NOPTS_VALUE)
         file.update_start_time();

      stime = av_rescale_q(file.ctx->start_time, AV_TIME_BASE_Q, st->time_base);
      stime2 = stime + (1ULL << st->pts_wrap_bits);
      wrap_correction_done = true;

      if (stime2 > stime && pkt.dts != AV_NOPTS_VALUE && pkt.dts > stime + (1LL << (st->pts_wrap_bits - 1)))
      {
         pkt.dts -= 1ULL << st->pts_wrap_bits;
         wrap_correction_done = false;
      }
      if (stime2 > stime && pkt.pts != AV_NOPTS_VALUE && pkt.pts > stime + (1LL << (st->pts_wrap_bits - 1)))
      {
         pkt.pts -= 1ULL << st->pts_wrap_bits;
         wrap_correction_done = false;
      }
   }

   /* add the stream-global side data to the first packet */
   if (nb_packets == 1)
   {
      if (st->nb_side_data)
         av_packet_split_side_data(&pkt);
      for (int i = 0; i < st->nb_side_data; i++)
      {
         AVPacketSideData *src_sd = &st->side_data[i];
         uint8_t *dst_data;

         if (av_packet_get_side_data(&pkt, src_sd->type, NULL))
            continue;
         if (autorotate && src_sd->type == AV_PKT_DATA_DISPLAYMATRIX)
            continue;

         dst_data = av_packet_new_side_data(&pkt, src_sd->type, src_sd->size);
         if (!dst_data)
            throw ffmpegException("Failed to allocate memory for side data.");

         std::copy_n(src_sd->data, src_sd->size, dst_data);
      }
   }

   if (pkt.dts != AV_NOPTS_VALUE)
      pkt.dts += av_rescale_q(ts_offset, AV_TIME_BASE_Q, st->time_base);
   if (pkt.pts != AV_NOPTS_VALUE)
      pkt.pts += av_rescale_q(ts_offset, AV_TIME_BASE_Q, st->time_base);

   if (pkt.pts != AV_NOPTS_VALUE)
      pkt.pts *= ts_scale;
   if (pkt.dts != AV_NOPTS_VALUE)
      pkt.dts *= ts_scale;

   int64_t pkt_dts = av_rescale_q_rnd(pkt.dts, st->time_base, AV_TIME_BASE_Q, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
   if ((dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO || dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) &&
       pkt_dts != AV_NOPTS_VALUE && next_dts == AV_NOPTS_VALUE && !copy_ts && (file.ctx->iformat->flags & AVFMT_TS_DISCONT) && last_ts != AV_NOPTS_VALUE)
   {
      int64_t delta = pkt_dts - last_ts;
      if (delta < -1LL * dts_delta_threshold * AV_TIME_BASE ||
          delta > 1LL * dts_delta_threshold * AV_TIME_BASE)
      {
         ts_offset -= delta;
         // av_log(NULL, AV_LOG_DEBUG, "Inter stream timestamp discontinuity %" PRId64 ", new offset= %" PRId64 "\n", delta, ifile->ts_offset);
         pkt.dts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
         if (pkt.pts != AV_NOPTS_VALUE)
            pkt.pts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
      }
   }

   int64_t duration = av_rescale_q(file.duration, file.time_base, st->time_base);
   if (pkt.pts != AV_NOPTS_VALUE)
   {
      pkt.pts += duration;
      max_pts = FFMAX(pkt.pts, max_pts);
      min_pts = FFMIN(pkt.pts, min_pts);
   }

   if (pkt.dts != AV_NOPTS_VALUE)
      pkt.dts += duration;

   pkt_dts = av_rescale_q_rnd(pkt.dts, st->time_base, AV_TIME_BASE_Q, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
   if ((dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO || dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) &&
       pkt_dts != AV_NOPTS_VALUE && next_dts != AV_NOPTS_VALUE && !copy_ts)
   {
      int64_t delta = pkt_dts - next_dts;
      if (file.ctx->iformat->flags & AVFMT_TS_DISCONT)
      {
         if (delta < -1LL * dts_delta_threshold * AV_TIME_BASE ||
             delta > 1LL * dts_delta_threshold * AV_TIME_BASE ||
             pkt_dts + AV_TIME_BASE / 10 < FFMAX(pts, dts))
         {
            ts_offset -= delta;
            // av_log(NULL, AV_LOG_DEBUG, "timestamp discontinuity %" PRId64 ", new offset= %" PRId64 "\n", delta, ifile->ts_offset);
            pkt.dts -= av_rescale_q(delta, AV_TIME_BASE_Q, st->time_base);
            if (pkt.pts != AV_NOPTS_VALUE)
               pkt.pts -= av_rescale_q(delta, AV_TIME_BASE_Q, st->time_base);
         }
      }
      else
      {
         if (delta < -1LL * dts_error_threshold * AV_TIME_BASE || delta > 1LL * dts_error_threshold * AV_TIME_BASE)
         {
            // av_log(NULL, AV_LOG_WARNING, "DTS %" PRId64 ", next:%" PRId64 " st:%d invalid dropping\n", pkt.dts, next_dts, pkt.stream_index);
            pkt.dts = AV_NOPTS_VALUE;
         }
         if (pkt.pts != AV_NOPTS_VALUE)
         {
            int64_t pkt_pts = av_rescale_q(pkt.pts, st->time_base, AV_TIME_BASE_Q);
            delta = pkt_pts - next_dts;
            if (delta < -1LL * dts_error_threshold * AV_TIME_BASE || delta > 1LL * dts_error_threshold * AV_TIME_BASE)
            {
               // av_log(NULL, AV_LOG_WARNING, "PTS %" PRId64 ", next:%" PRId64 " invalid dropping st:%d\n", pkt.pts, next_dts, pkt.stream_index);
               pkt.pts = AV_NOPTS_VALUE;
            }
         }
      }
   }

   if (pkt.dts != AV_NOPTS_VALUE)
      last_ts = av_rescale_q(pkt.dts, st->time_base, AV_TIME_BASE_Q);

   // if (debug_ts)
   // {
   //    av_log(NULL, AV_LOG_INFO, "demuxer+ffmpeg -> ist_index:%d type:%s pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s off:%s off_time:%s\n",
   //           ifile->ist_index + pkt.stream_index, av_get_media_type_string(ist->dec_ctx->codec_type),
   //           av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &ist->st->time_base),
   //           av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &ist->st->time_base),
   //           av_ts2str(input_files[ist->file_index]->ts_offset),
   //           av_ts2timestr(input_files[ist->file_index]->ts_offset, &AV_TIME_BASE_Q));
   // }

   return true;
}

int InputStream::flush(bool no_eof) // flush decoder
{
   int ret;
   if (decoding_needed)
   {
      ret = process_packet(NULL, no_eof);

      if (no_eof)
         avcodec_flush_buffers(dec_ctx.get());
      else if (ret > 0)
         return 0;
   }

   if (!no_eof) // if EOF, finish the related output streams
   {
      /* mark all outputs that don't go through lavfi as finished */
      for (auto ost = osts.begin(); ost < osts.end(); ost++)
      {
         if (dec_ctx->codec_type == AVMEDIA_TYPE_SUBTITLE || ost->stream_copy)
            (*ost)->finish();
      }
   }

   return ret;
}

   int InputStream::check_stream_specifier(const std::string spec)
   {
      int ret = avformat_match_stream_specifier(file.ctx.get(), st, spec.c_str());
      // if (ret < 0)
      //    av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
      return ret;
   }

   void InputStream::input_to_filter(InputFilter &new_filter) // set stream to output to a filter
   {
      decoding_needed |= DECODING_FOR_FILTER;

      discard = false;
      st->discard = AVDISCARD_NONE;

      filters.push_back(new_filter);
   }

AVRational InputStream::get_framerate() const
{
    return av_guess_frame_rate(file->ctx, st, NULL);
}

double InputStream::get_rotation()
{
   AVDictionaryEntry *rotate_tag = av_dict_get(st->metadata, "rotate", NULL, 0);
   uint8_t *displaymatrix = av_stream_get_side_data(st, AV_PKT_DATA_DISPLAYMATRIX, NULL);
   double theta = 0;

   if (rotate_tag && *rotate_tag->value && strcmp(rotate_tag->value, "0"))
   {
      char *tail;
      theta = av_strtod(rotate_tag->value, &tail);
      if (*tail)
         theta = 0;
   }
   if (displaymatrix && !theta)
      theta = -av_display_rotation_get((int32_t *)displaymatrix);

   theta -= 360 * floor(theta / 360 + 0.9 / 360);

   // if (fabs(theta - 90 * round(theta / 90)) > 2)
   //    av_log(NULL, AV_LOG_WARNING, "Odd rotation angle.\n"
   //                                 "If you want to help, upload a sample "
   //                                 "of this file to ftp://upload.ffmpeg.org/incoming/ "
   //                                 "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)");

   return theta;
}
