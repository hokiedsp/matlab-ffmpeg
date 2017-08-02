#include "ffmpegInputFile.h"

#include <cstring>

extern "C" {
#include <libavutil/time.h>
}

#include "ffmpegPtrs.h"
#include "ffmpegUtil.h"
#include "ffmpegAvRedefine.h"

using namespace ffmpeg;

InputFileSelectStream::InputFileSelectStream(const std::string &filename, AVMediaType type, int st_index)
    : fmt_ctx(NULL, delete_input_ctx), st(NULL), dec(NULL), dec_ctx(NULL, delete_codec_ctx),
      decoded_frame(NULL), filter_frame(NULL),
      accurate_seek(0),
      loop(0),
      thread_queue_size(8), non_blocking(false)
{
   // create new file format context
   open_file(filename);

   // select a stream defined in the file as specified and create new codec context
   select_stream(type, st_index);
}

void InputFileSelectStream::open_file(const std::string &filename)
{
   /* get default parameters from command line */
   fmt_ctx.reset(avformat_alloc_context());
   if (!fmt_ctx.get())
      throw ffmpegException(filename, AVERROR(ENOMEM));

   fmt_ctx->flags |= AVFMT_FLAG_NONBLOCK;
   fmt_ctx->interrupt_callback = {NULL, NULL}; // from ffmpegBase

   ////////////////////

   DictPtr format_opts = std::make_unique<AVDictionary, delete_dict>();
   av_dict_set(*format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);

   /* open the input file with generic avformat function */
   AVFormatContext *ic = fmt_ctx.release();
   int err;
   if ((err = avformat_open_input(&ic, filename.c_str(), NULL, &format_opts)) < 0)
      throw ffmpegException(filename, err);
   fmt_ctx.reset(ic);
}

/* Add all the streams from the given input file to the global
 * list of input streams. */
void InputFileSelectStream::select_stream(AVMediaType type, int index)
{
   AVFormatContext *ic = fmt_ctx.get();

   int i = 0;
   int count = 0;
   for (; i < (int)ic->nb_streams; i++) // for each stream
   {
      // look for a stream which matches the
      if (ic->streams[i]->codecpar->codec_type == type && count++ == index)
      {
         stream_index = i;
         st = ic->streams[i];
         switch (type)
         {
         case AVMEDIA_TYPE_VIDEO:
            decode = &decode_video;
            break;
         case AVMEDIA_TYPE_AUDIO:
            decode = &decode_audio;
            break;
         case AVMEDIA_TYPE_SUBTITLE:
         case AVMEDIA_TYPE_DATA:
         case AVMEDIA_TYPE_ATTACHMENT:
         case AVMEDIA_TYPE_UNKNOWN:
         default:
            throw ffmpegException("Unsupported decoder media type.");
         }
      }
      else
      {
         // all other streams are ignored
         ic->streams[i]->discard = AVDISCARD_ALL;
      }
   }
   if (!count)
      ffmpegException("Media file does not include the requested media type.")
}
}

////////////////////////////

void InputFileSelectStream::seek(const int64_t timestamp)
{
   int64_t seek_timestamp = timestamp;
   if (!(fmt_ctx->iformat->flags & AVFMT_SEEK_TO_PTS)) // if seeking is not based on presentation timestamp (PTS)
   {
      bool dts_heuristic = false;
      for (int i = 0; i < (int)fmt_ctx->nb_streams; i++)
      {
         if (fmt_ctx->streams[i]->codecpar->video_delay)
            dts_heuristic = true;
      }
      if (dts_heuristic)
         seek_timestamp -= 3 * AV_TIME_BASE / 23;
   }

   if (avformat_seek_file(fmt_ctx.get(), stream.index, INT64_MIN, seek_timestamp, seek_timestamp, 0) < 0)
   {
      std::ostringstream msg;
      msg << "Could not seek to position " << (double)timestamp / AV_TIME_BASE;
      throw ffmpegException(msg.str());
   }
}

////////////////////////////

int InputFileSelectStream::get_packet(AVPacket &pkt)
{
#define RECVPKT(p) av_thread_message_queue_recv(in_thread_queue, &p, non_blocking ? AV_THREAD_MESSAGE_NONBLOCK : 0)

   // receive the frame/packet from one of input threads
   int ret = RECVPKT(pkt);
   if (ret == AVERROR(EAGAIN)) // frame not available now
   {
      eagain = true;
      return ret;
   }
   if (ret == AVERROR_EOF && loop) // if reached EOF and loop playback mode
   {
      seek(0);            // rewind the stream
      ret = RECVPKT(pkt); // see if the first frame is already available
      if (ret == AVERROR(EAGAIN))
      {
         eagain = true;
         return ret;
      }
   }
   if (ret == AVERROR_EOF) // if end-of-file
   {
      // if end-of-file reached, flush all input & output streams
      stream.flush(false);

      eof_reached = true;
      return AVERROR(EAGAIN);
   }
   else if (ret < 0) // if not end-of-file, a fatal error has occurred
   {
      throw ffmpegException(fmt_ctx->filename, ret);
   }
}

/*
 * Return
 * - 0 -- one packet was read and processed
 * - AVERROR(EAGAIN) -- no packets were available for selected file,
 *   this function should be called again
 * - AVERROR_EOF -- this function should not be called again
 */
void InputFileSelectStream::prepare_packet(AVPacket &pkt)
{
   if (pkt.stream_index == stream_index)
      stream.prepare_packet(&pkt, false); // decode if requested
}

/////////////////////////////////////////////////////////

void InputFileSelectStream::input_thread()
{
   unsigned flags = non_blocking ? AV_THREAD_MESSAGE_NONBLOCK : 0;
   read_state = 0;

   while (1)
   {
      AVPacket pkt;

      // check for the buffer availability

      // read next frame
      read_state = av_read_frame(fmt_ctx.get(), &pkt);
      if (read_state == AVERROR(EAGAIN)) // frame not ready, wait 10 ms
      {
         av_usleep(10000);
         continue;
      }

      if (read_state < 0) // unexcepted error occurred, throw the error over the message queue and quit
      {
         av_thread_message_queue_set_err_recv(in_thread_queue, read_state);
         break;
      }

      // if received packet is not of the stream, read next packet
      if (pkt.stream_index != stream_index)
         continue;

      // send decoded frame to the message queue
      read_state = av_thread_message_queue_send(in_thread_queue, &pkt, flags);
      if (flags && ret == AVERROR(EAGAIN)) // queue overflowed
      {                                    // try it again BLOCKING
         flags = 0;
         read_state = av_thread_message_queue_send(in_thread_queue, &pkt, flags);
         //av_log(fmt_ctx, AV_LOG_WARNING, "Thread message queue blocking; consider raising the thread_queue_size option (current value: %d)\n", thread_queue_size);
      }
      if (read_state < 0)
      {
         // if (ret != AVERROR_EOF)
         //    av_log(fmt_ctx, AV_LOG_ERROR, "Unable to send packet to main thread: %s\n", av_err2str(ret));
         av_packet_unref(&pkt); // let go of the packet object
         av_thread_message_queue_set_err_recv(in_thread_queue, ret);
         break;
      }
   }
}

void InputFileSelectStream::init_thread()
{
   if (fmt_ctx->pb ? !fmt_ctx->pb->seekable : std::strcmp(fmt_ctx->iformat->name, "lavfi"))
      non_blocking = true;

   int ret;
   if (ret = av_thread_message_queue_alloc(&in_thread_queue, thread_queue_size, sizeof(AVPacket)) < 0)
      throw ffmpegException(ret);

   thread = std::thread(&InputFileSelectStream::input_thread, this);
}

void InputFileSelectStream::free_thread(void)
{
   AVPacket pkt;

   if (!in_thread_queue)
      return;

   av_thread_message_queue_set_err_send(in_thread_queue, AVERROR_EOF);
   while (av_thread_message_queue_recv(in_thread_queue, &pkt, 0) >= 0)
      av_packet_unref(&pkt);

   thread.join();
   av_thread_message_queue_free(&in_thread_queue);
}

////////////////////////////////////////////////////////////////////////////////////////////////

// This does not quite work like avcodec_decode_audio4/avcodec_decode_video2.
// There is the following difference: if you got a frame, you must call
// it again with pkt=NULL. pkt==NULL is treated differently from pkt.size==0
// (pkt==NULL means get more output, pkt.size==0 is a flush/drain packet)
int InputFileSelectStream::decode_frame(AVFrame *frame, bool &got_frame, const AVPacket *pkt)
{
   int ret;

   got_frame = false;

   if (pkt)
   {
      ret = avcodec_send_packet(dec_ctx->get(), pkt);
      // In particular, we don't expect AVERROR(EAGAIN), because we read all
      // decoded frames with avcodec_receive_frame() until done.
      if (ret < 0 && ret != AVERROR_EOF)
         return ret;
   }

   ret = avcodec_receive_frame(dec_ctx->get(), frame);
   if (ret < 0 && ret != AVERROR(EAGAIN))
      return ret;
   if (ret >= 0)
      got_frame = true;

   return 0;
}

int InputFileSelectStream::decode_audio(AVPacket *pkt, bool &got_output, int eof)
{
   AVFrame *decoded_frame, *f;
   AVCodecContext *avctx = dec_ctx->get();
   int i, ret, err = 0, resample_changed;
   AVRational decoded_frame_tb;

   if (!ist->decoded_frame && !(ist->decoded_frame = av_frame_alloc()))
      return AVERROR(ENOMEM);
   if (!ist->filter_frame && !(ist->filter_frame = av_frame_alloc()))
      return AVERROR(ENOMEM);
   decoded_frame = ist->decoded_frame;

   ret = decode_frame(avctx, decoded_frame, got_output, pkt);

   if (ret >= 0 && avctx->sample_rate <= 0)
   {
      av_log(avctx, AV_LOG_ERROR, "Sample rate %d invalid\n", avctx->sample_rate);
      ret = AVERROR_INVALIDDATA;
   }

   if (ret != AVERROR_EOF)
      check_decode_result(ist, got_output, ret);

   if (!got_output || ret < 0)
      return ret;

   ist->samples_decoded += decoded_frame->nb_samples;
   ist->frames_decoded++;

#if 1
   /* increment next_dts to use for the case where the input stream does not
       have timestamps or there are multiple frames in the packet */
   ist->next_pts += ((int64_t)AV_TIME_BASE * decoded_frame->nb_samples) /
                    avctx->sample_rate;
   ist->next_dts += ((int64_t)AV_TIME_BASE * decoded_frame->nb_samples) /
                    avctx->sample_rate;
#endif

   resample_changed = ist->resample_sample_fmt != decoded_frame->format ||
                      ist->resample_channels != avctx->channels ||
                      ist->resample_channel_layout != decoded_frame->channel_layout ||
                      ist->resample_sample_rate != decoded_frame->sample_rate;
   if (resample_changed)
   {
      char layout1[64], layout2[64];

      if (!guess_input_channel_layout(ist))
      {
         av_log(NULL, AV_LOG_FATAL, "Unable to find default channel "
                                    "layout for Input Stream #%d.%d\n",
                ist->file_index,
                ist->st->index);
         exit_program(1);
      }
      decoded_frame->channel_layout = avctx->channel_layout;

      av_get_channel_layout_string(layout1, sizeof(layout1), ist->resample_channels,
                                   ist->resample_channel_layout);
      av_get_channel_layout_string(layout2, sizeof(layout2), avctx->channels,
                                   decoded_frame->channel_layout);

      av_log(NULL, AV_LOG_INFO,
             "Input stream #%d:%d frame changed from rate:%d fmt:%s ch:%d chl:%s to rate:%d fmt:%s ch:%d chl:%s\n",
             ist->file_index, ist->st->index,
             ist->resample_sample_rate, av_get_sample_fmt_name(ist->resample_sample_fmt),
             ist->resample_channels, layout1,
             decoded_frame->sample_rate, av_get_sample_fmt_name(decoded_frame->format),
             avctx->channels, layout2);

      ist->resample_sample_fmt = decoded_frame->format;
      ist->resample_sample_rate = decoded_frame->sample_rate;
      ist->resample_channel_layout = decoded_frame->channel_layout;
      ist->resample_channels = avctx->channels;

      for (i = 0; i < nb_filtergraphs; i++)
         if (ist_in_filtergraph(filtergraphs[i], ist))
         {
            FilterGraph *fg = filtergraphs[i];
            if (configure_filtergraph(fg) < 0)
            {
               av_log(NULL, AV_LOG_FATAL, "Error reinitializing filters!\n");
               exit_program(1);
            }
         }
   }

   if (decoded_frame->pts != AV_NOPTS_VALUE)
   {
      decoded_frame_tb = ist->st->time_base;
   }
   else if (pkt && pkt->pts != AV_NOPTS_VALUE)
   {
      decoded_frame->pts = pkt->pts;
      decoded_frame_tb = ist->st->time_base;
   }
   else
   {
      decoded_frame->pts = ist->dts;
      decoded_frame_tb = AV_TIME_BASE_Q;
   }
   if (decoded_frame->pts != AV_NOPTS_VALUE)
      decoded_frame->pts = av_rescale_delta(decoded_frame_tb, decoded_frame->pts,
                                            (AVRational){1, avctx->sample_rate},
                                            decoded_frame->nb_samples,
                                            &ist->filter_in_rescale_delta_last,
                                            (AVRational){1, avctx->sample_rate});
   ist->nb_samples = decoded_frame->nb_samples;
   for (i = 0; i < ist->nb_filters; i++)
   {
      if (i < ist->nb_filters - 1)
      {
         f = ist->filter_frame;
         err = av_frame_ref(f, decoded_frame);
         if (err < 0)
            break;
      }
      else
         f = decoded_frame;
      err = av_buffersrc_add_frame_flags(ist->filters[i]->filter, f,
                                         AV_BUFFERSRC_FLAG_PUSH);
      if (err == AVERROR_EOF)
         err = 0; /* ignore */
      if (err < 0)
         break;
   }
   decoded_frame->pts = AV_NOPTS_VALUE;

   av_frame_unref(ist->filter_frame);
   av_frame_unref(decoded_frame);
   return err < 0 ? err : ret;
}

int InputFileSelectStream::decode_video(AVPacket *pkt, bool &got_output, int eof)
{
   AVFrame *decoded_frame, *f;
   int i, ret = 0, err = 0, resample_changed;
   int64_t best_effort_timestamp;
   int64_t dts = AV_NOPTS_VALUE;
   AVRational *frame_sample_aspect;
   AVPacket avpkt;

   // With fate-indeo3-2, we're getting 0-sized packets before EOF for some
   // reason. This seems like a semi-critical bug. Don't trigger EOF, and
   // skip the packet.
   if (!eof && pkt && pkt->size == 0)
      return 0;

   if (!ist->decoded_frame && !(ist->decoded_frame = av_frame_alloc()))
      return AVERROR(ENOMEM);
   if (!ist->filter_frame && !(ist->filter_frame = av_frame_alloc()))
      return AVERROR(ENOMEM);
   decoded_frame = ist->decoded_frame;
   if (ist->dts != AV_NOPTS_VALUE)
      dts = av_rescale_q(ist->dts, AV_TIME_BASE_Q, ist->st->time_base);
   if (pkt)
   {
      avpkt = *pkt;
      avpkt.dts = dts; // ffmpeg.c probably shouldn't do this
   }

   // The old code used to set dts on the drain packet, which does not work
   // with the new API anymore.
   if (eof)
   {
      void *new = av_realloc_array(ist->dts_buffer, ist->nb_dts_buffer + 1, sizeof(ist->dts_buffer[0]));
      if (!new)
         return AVERROR(ENOMEM);
      ist->dts_buffer = new;
      ist->dts_buffer[ist->nb_dts_buffer++] = dts;
   }

   ret = decode_frame(ist->dec_ctx, decoded_frame, got_output, pkt ? &avpkt : NULL);

   // The following line may be required in some cases where there is no parser
   // or the parser does not has_b_frames correctly
   if (ist->st->codecpar->video_delay < ist->dec_ctx->has_b_frames)
   {
      if (ist->dec_ctx->codec_id == AV_CODEC_ID_H264)
      {
         ist->st->codecpar->video_delay = ist->dec_ctx->has_b_frames;
      }
      else
         av_log(ist->dec_ctx, AV_LOG_WARNING,
                "video_delay is larger in decoder than demuxer %d > %d.\n"
                "If you want to help, upload a sample "
                "of this file to ftp://upload.ffmpeg.org/incoming/ "
                "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)",
                ist->dec_ctx->has_b_frames,
                ist->st->codecpar->video_delay);
   }

   if (ret != AVERROR_EOF)
      check_decode_result(ist, got_output, ret);

   if (got_output && ret >= 0)
   {
      if (ist->dec_ctx->width != decoded_frame->width ||
          ist->dec_ctx->height != decoded_frame->height ||
          ist->dec_ctx->pix_fmt != decoded_frame->format)
      {
         av_log(NULL, AV_LOG_DEBUG, "Frame parameters mismatch context %d,%d,%d != %d,%d,%d\n",
                decoded_frame->width,
                decoded_frame->height,
                decoded_frame->format,
                ist->dec_ctx->width,
                ist->dec_ctx->height,
                ist->dec_ctx->pix_fmt);
      }
   }

   if (!got_output || ret < 0)
      return ret;

   if (ist->top_field_first >= 0)
      decoded_frame->top_field_first = ist->top_field_first;

   ist->frames_decoded++;

   if (ist->hwaccel_retrieve_data && decoded_frame->format == ist->hwaccel_pix_fmt)
   {
      err = ist->hwaccel_retrieve_data(ist->dec_ctx, decoded_frame);
      if (err < 0)
         goto fail;
   }
   ist->hwaccel_retrieved_pix_fmt = decoded_frame->format;

   best_effort_timestamp = av_frame_get_best_effort_timestamp(decoded_frame);

   if (eof && best_effort_timestamp == AV_NOPTS_VALUE && ist->nb_dts_buffer > 0)
   {
      best_effort_timestamp = ist->dts_buffer[0];

      for (i = 0; i < ist->nb_dts_buffer - 1; i++)
         ist->dts_buffer[i] = ist->dts_buffer[i + 1];
      ist->nb_dts_buffer--;
   }

   if (best_effort_timestamp != AV_NOPTS_VALUE)
   {
      int64_t ts = av_rescale_q(decoded_frame->pts = best_effort_timestamp, ist->st->time_base, AV_TIME_BASE_Q);

      if (ts != AV_NOPTS_VALUE)
         ist->next_pts = ist->pts = ts;
   }

   if (debug_ts)
   {
      av_log(NULL, AV_LOG_INFO, "decoder -> ist_index:%d type:video "
                                "frame_pts:%s frame_pts_time:%s best_effort_ts:%" PRId64 " best_effort_ts_time:%s keyframe:%d frame_type:%d time_base:%d/%d\n",
             ist->st->index, av_ts2str(decoded_frame->pts),
             av_ts2timestr(decoded_frame->pts, &ist->st->time_base),
             best_effort_timestamp,
             av_ts2timestr(best_effort_timestamp, &ist->st->time_base),
             decoded_frame->key_frame, decoded_frame->pict_type,
             ist->st->time_base.num, ist->st->time_base.den);
   }

   if (ist->st->sample_aspect_ratio.num)
      decoded_frame->sample_aspect_ratio = ist->st->sample_aspect_ratio;

   resample_changed = ist->resample_width != decoded_frame->width ||
                      ist->resample_height != decoded_frame->height ||
                      ist->resample_pix_fmt != decoded_frame->format;
   if (resample_changed)
   {
      av_log(NULL, AV_LOG_INFO,
             "Input stream #%d:%d frame changed from size:%dx%d fmt:%s to size:%dx%d fmt:%s\n",
             ist->file_index, ist->st->index,
             ist->resample_width, ist->resample_height, av_get_pix_fmt_name(ist->resample_pix_fmt),
             decoded_frame->width, decoded_frame->height, av_get_pix_fmt_name(decoded_frame->format));

      ist->resample_width = decoded_frame->width;
      ist->resample_height = decoded_frame->height;
      ist->resample_pix_fmt = decoded_frame->format;

      for (i = 0; i < nb_filtergraphs; i++)
      {
         if (ist_in_filtergraph(filtergraphs[i], ist) && ist->reinit_filters &&
             configure_filtergraph(filtergraphs[i]) < 0)
         {
            av_log(NULL, AV_LOG_FATAL, "Error reinitializing filters!\n");
            exit_program(1);
         }
      }
   }

   frame_sample_aspect = av_opt_ptr(avcodec_get_frame_class(), decoded_frame, "sample_aspect_ratio");
   for (i = 0; i < ist->nb_filters; i++)
   {
      if (!frame_sample_aspect->num)
         *frame_sample_aspect = ist->st->sample_aspect_ratio;

      if (i < ist->nb_filters - 1)
      {
         f = ist->filter_frame;
         err = av_frame_ref(f, decoded_frame);
         if (err < 0)
            break;
      }
      else
         f = decoded_frame;
      err = av_buffersrc_add_frame_flags(ist->filters[i]->filter, f, AV_BUFFERSRC_FLAG_PUSH);
      if (err == AVERROR_EOF)
      {
         err = 0; /* ignore */
      }
      else if (err < 0)
      {
         av_log(NULL, AV_LOG_FATAL,
                "Failed to inject frame into filter network: %s\n", av_err2str(err));
         exit_program(1);
      }
   }

fail:
   av_frame_unref(ist->filter_frame);
   av_frame_unref(decoded_frame);
   return err < 0 ? err : ret;
}
