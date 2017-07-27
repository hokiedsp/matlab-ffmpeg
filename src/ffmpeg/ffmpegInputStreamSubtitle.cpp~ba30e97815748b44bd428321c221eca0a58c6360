#include "ffmpegInputStream.h"

extern "C" {
#include <libavutil/avassert.h>
#include <libavfilter/buffersrc.h>
}

#include "ffmpegInputFile.h"
#include "ffmpegAvRedefine.h"

using namespace ffmpeg;

SubtitleInputStream::SubtitleInputStream(InputFile &infile, const int i, const InputOptionsContext &o)
    : DataInputStream(infile, i, o)
{
   const Option *opt = NULL;

   if (opt = o.cfind("autorotate"))
      autorotate = ((OptionBool *)opt)->value;
}

int SubtitleInputStream::decode_packet(const AVPacket *inpkt, bool repeating, bool &got_output)
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

   int ret = 0;
   if (repeating)
      return ret;
   ret = transcode_subtitles(&avpkt, got_output);
   if (!inpkt && ret >= 0)
      ret = AVERROR_EOF;
   return ret;
}

int SubtitleInputStream::transcode_subtitles(AVPacket *pkt, bool &got_output)
{
   int got_sub = got_output;
   int ret = avcodec_decode_subtitle2(dec_ctx.get(), &subtitle, &got_sub, pkt);
   got_output = got_sub;
   if (got_output || ret < 0)
      decode_error_stat[ret < 0]++;

   if (ret < 0 || !got_output)
   {
      if (!pkt->size)
         sub2video_flush();
      return ret;
   }

   if (fix_sub_duration)
   {
      uint32_t end = 1;
      if (prev_sub.got_output)
      {
         end = (uint32_t) av_rescale(subtitle.pts - prev_sub.subtitle.pts, 1000, AV_TIME_BASE);
         if (end < prev_sub.subtitle.end_display_time)
         {
            // av_log(dec_ctx.get(), AV_LOG_DEBUG,
            //        "Subtitle duration reduced from %d to %d%s\n",
            //        prev_sub.subtitle.end_display_time, end,
            //        end <= 0 ? ", dropping it" : "");
            prev_sub.subtitle.end_display_time = end;
         }
      }
      FFSWAP(bool, got_output, prev_sub.got_output);
      FFSWAP(int, ret, prev_sub.ret);
      FFSWAP(AVSubtitle, subtitle, prev_sub.subtitle);
      if (end <= 0)
         goto out;
   }

   if (!got_output)
      return ret;

   sub2video_update(&subtitle);

   if (!subtitle.num_rects)
      goto out;

   frames_decoded++;

   // outputs to all the destination streams
   // for (int i = 0; i < nb_output_streams; i++)
   // {
   //    OutputStream *ost = output_streams[i];

   //    if (!check_output_constraints(this, ost) || !ost->encoding_needed || ost->enc->type != AVMEDIA_TYPE_SUBTITLE)
   //       continue;

   //    do_subtitle_out(output_files[ost->file_index], ost, &subtitle);
   // }

out:
   avsubtitle_free(&subtitle);
   return ret;
}
/* sub2video hack:
   Convert subtitles to video with alpha to insert them in filter graphs.
   This is a temporary solution until libavfilter gets real subtitles support.
 */

int SubtitleInputStream::sub2video_get_blank_frame()
{
   int ret;
   AVFrame *frame = sub2video.frame;

   av_frame_unref(frame);
   sub2video.frame->width = dec_ctx->width ? dec_ctx->width : sub2video.w;
   sub2video.frame->height = dec_ctx->height ? dec_ctx->height : sub2video.h;
   sub2video.frame->format = AV_PIX_FMT_RGB32;
   if ((ret = av_frame_get_buffer(frame, 32)) < 0)
      return ret;
   memset(frame->data[0], 0, frame->height * frame->linesize[0]);
   return 0;
}

void SubtitleInputStream::sub2video_copy_rect(uint8_t *dst, int dst_linesize, int w, int h, AVSubtitleRect *r)
{
   uint32_t *pal, *dst2;
   uint8_t *src, *src2;
   int x, y;

   if (r->type != SUBTITLE_BITMAP)
   {
      av_log(NULL, AV_LOG_WARNING, "sub2video: non-bitmap subtitle\n");
      return;
   }
   if (r->x < 0 || r->x + r->w > w || r->y < 0 || r->y + r->h > h)
   {
      av_log(NULL, AV_LOG_WARNING, "sub2video: rectangle (%d %d %d %d) overflowing %d %d\n",
             r->x, r->y, r->w, r->h, w, h);
      return;
   }

   dst += r->y * dst_linesize + r->x * 4;
   src = r->data[0];
   pal = (uint32_t *)r->data[1];
   for (y = 0; y < r->h; y++)
   {
      dst2 = (uint32_t *)dst;
      src2 = src;
      for (x = 0; x < r->w; x++)
         *(dst2++) = pal[*(src2++)];
      dst += dst_linesize;
      src += r->linesize[0];
   }
}

void SubtitleInputStream::sub2video_push_ref(int64_t pts)
{
   AVFrame *frame = sub2video.frame;

   av_assert1(frame->data[0]);
   sub2video.last_pts = frame->pts = pts;
   for (auto filt = filters.begin(); filt < filters.end(); filt++)
      av_buffersrc_add_frame_flags((*filt)->filter, frame,
                                   AV_BUFFERSRC_FLAG_KEEP_REF | AV_BUFFERSRC_FLAG_PUSH);
}

void SubtitleInputStream::sub2video_update(AVSubtitle *sub)
{
   AVFrame *frame = sub2video.frame;
   int8_t *dst;
   int dst_linesize;
   int num_rects, i;
   int64_t pts, end_pts;

   if (!frame)
      return;
   if (sub)
   {
      pts = av_rescale_q(sub->pts + sub->start_display_time * 1000LL,
                         AV_TIME_BASE_Q, st->time_base);
      end_pts = av_rescale_q(sub->pts + sub->end_display_time * 1000LL,
                             AV_TIME_BASE_Q, st->time_base);
      num_rects = sub->num_rects;
   }
   else
   {
      pts = sub2video.end_pts;
      end_pts = INT64_MAX;
      num_rects = 0;
   }
   if (sub2video_get_blank_frame() < 0)
   {
      av_log(dec_ctx.get(), AV_LOG_ERROR, "Impossible to get a blank canvas.\n");
      return;
   }
   dst = (int8_t*)frame->data[0];
   dst_linesize = frame->linesize[0];
   for (i = 0; i < num_rects; i++)
      sub2video_copy_rect((uint8_t*)dst, dst_linesize, frame->width, frame->height, sub->rects[i]);
   sub2video_push_ref(pts);
   sub2video.end_pts = end_pts;
}

void SubtitleInputStream::sub2video_heartbeat(int64_t pts)
{
   int nb_reqs;
   int64_t pts2;

   /* When a frame is read from a file, examine all sub2video streams in
       the same file and send the sub2video frame again. Otherwise, decoded
       video frames could be accumulating in the filter graph while a filter
       (possibly overlay) is desperately waiting for a subtitle frame. */
   for (auto ist = file.streams.begin(); ist<file.streams.end();ist++)
   {
      if ((*ist)->get_codec_type()!=AVMEDIA_TYPE_SUBTITLE) continue;

      SubtitleInputStream &sist = *(SubtitleInputStream*)(ist->get());

      if (!sist.sub2video.frame)
         continue;
      /* subtitles seem to be usually muxed ahead of other streams;
           if not, subtracting a larger time here is necessary */
      pts2 = av_rescale_q(pts, st->time_base, sist.st->time_base) - 1;
      /* do not send the heartbeat frame if the subtitle is already ahead */
      if (pts2 <= sist.sub2video.last_pts)
         continue;
      if (pts2 >= sist.sub2video.end_pts || !sist.sub2video.frame->data[0])
         sist.sub2video_update(NULL);
      
      nb_reqs = 0;
      for (auto filt = sist.filters.begin(); filt < sist.filters.end(); filt++)
         nb_reqs += av_buffersrc_get_nb_failed_requests((*filt)->filter);
      if (nb_reqs)
         sist.sub2video_push_ref(pts2);
   }
}

void SubtitleInputStream::sub2video_flush()
{
   if (sub2video.end_pts < INT64_MAX)
      sub2video_update(NULL);
   for (auto filt = filters.begin(); filt<filters.end();filt++)
      av_buffersrc_add_frame((*filt)->filter, NULL);
}

/* pkt = NULL means EOF (needed to flush decoder buffers) */
int SubtitleInputStream::prepare_packet(const AVPacket *pkt, bool no_eof)
{
   if (pkt)
      sub2video_heartbeat(pkt->pts);

   return InputStream::prepare_packet(pkt, no_eof);
}
