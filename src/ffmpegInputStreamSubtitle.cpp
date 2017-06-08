#include "ffmpegInputStream.h"

SubtitleInputStream::SubtitleInputStream(InputFile &infile, const int i, const InputOptionsContext &o) : DataInputStream(infile, i, o)
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

   int ret;
   if (repeating)
      break;
   ret = transcode_subtitles(ist, &avpkt, &got_output);
   if (!pkt && ret >= 0)
      ret = AVERROR_EOF;
   break;
}

static int transcode_subtitles(AVPacket *pkt, int *got_output)
{
   
   int ret = avcodec_decode_subtitle2(dec_ctx, &subtitle, got_output, pkt);

   check_decode_result(NULL, got_output, ret);

   if (ret < 0 || !*got_output)
   {
      if (!pkt->size)
         sub2video_flush(ist);
      return ret;
   }

   if (fix_sub_duration)
   {
      int end = 1;
      if (prev_sub.got_output)
      {
         end = av_rescale(subtitle.pts - prev_sub.subtitle.pts, 1000, AV_TIME_BASE);
         if (end < prev_sub.subtitle.end_display_time)
         {
            av_log(dec_ctx, AV_LOG_DEBUG,
                   "Subtitle duration reduced from %d to %d%s\n",
                   prev_sub.subtitle.end_display_time, end,
                   end <= 0 ? ", dropping it" : "");
            prev_sub.subtitle.end_display_time = end;
         }
      }
      FFSWAP(int, *got_output, prev_sub.got_output);
      FFSWAP(int, ret, prev_sub.ret);
      FFSWAP(AVSubtitle, subtitle, prev_sub.subtitle);
      if (end <= 0)
         goto out;
   }

   if (!*got_output)
      return ret;

   sub2video_update(this, &subtitle);

   if (!subtitle.num_rects)
      goto out;

   frames_decoded++;

   for (int i = 0; i < nb_output_streams; i++)
   {
      OutputStream *ost = output_streams[i];

      if (!check_output_constraints(this, ost) || !ost->encoding_needed || ost->enc->type != AVMEDIA_TYPE_SUBTITLE)
         continue;

      do_subtitle_out(output_files[ost->file_index], ost, &subtitle);
   }

out:
   avsubtitle_free(&subtitle);
   return ret;
}
/* sub2video hack:
   Convert subtitles to video with alpha to insert them in filter graphs.
   This is a temporary solution until libavfilter gets real subtitles support.
 */

static int sub2video_get_blank_frame(InputStream *ist)
{
    int ret;
    AVFrame *frame = ist->sub2video.frame;

    av_frame_unref(frame);
    ist->sub2video.frame->width  = ist->dec_ctx->width  ? ist->dec_ctx->width  : ist->sub2video.w;
    ist->sub2video.frame->height = ist->dec_ctx->height ? ist->dec_ctx->height : ist->sub2video.h;
    ist->sub2video.frame->format = AV_PIX_FMT_RGB32;
    if ((ret = av_frame_get_buffer(frame, 32)) < 0)
        return ret;
    memset(frame->data[0], 0, frame->height * frame->linesize[0]);
    return 0;
}

static void sub2video_copy_rect(uint8_t *dst, int dst_linesize, int w, int h,
                                AVSubtitleRect *r)
{
    uint32_t *pal, *dst2;
    uint8_t *src, *src2;
    int x, y;

    if (r->type != SUBTITLE_BITMAP) {
        av_log(NULL, AV_LOG_WARNING, "sub2video: non-bitmap subtitle\n");
        return;
    }
    if (r->x < 0 || r->x + r->w > w || r->y < 0 || r->y + r->h > h) {
        av_log(NULL, AV_LOG_WARNING, "sub2video: rectangle (%d %d %d %d) overflowing %d %d\n",
            r->x, r->y, r->w, r->h, w, h
        );
        return;
    }

    dst += r->y * dst_linesize + r->x * 4;
    src = r->data[0];
    pal = (uint32_t *)r->data[1];
    for (y = 0; y < r->h; y++) {
        dst2 = (uint32_t *)dst;
        src2 = src;
        for (x = 0; x < r->w; x++)
            *(dst2++) = pal[*(src2++)];
        dst += dst_linesize;
        src += r->linesize[0];
    }
}

static void sub2video_push_ref(InputStream *ist, int64_t pts)
{
    AVFrame *frame = ist->sub2video.frame;
    int i;

    av_assert1(frame->data[0]);
    ist->sub2video.last_pts = frame->pts = pts;
    for (i = 0; i < ist->nb_filters; i++)
        av_buffersrc_add_frame_flags(ist->filters[i]->filter, frame,
                                     AV_BUFFERSRC_FLAG_KEEP_REF |
                                     AV_BUFFERSRC_FLAG_PUSH);
}

static void sub2video_update(InputStream *ist, AVSubtitle *sub)
{
    AVFrame *frame = ist->sub2video.frame;
    int8_t *dst;
    int     dst_linesize;
    int num_rects, i;
    int64_t pts, end_pts;

    if (!frame)
        return;
    if (sub) {
        pts       = av_rescale_q(sub->pts + sub->start_display_time * 1000LL,
                                 AV_TIME_BASE_Q, ist->st->time_base);
        end_pts   = av_rescale_q(sub->pts + sub->end_display_time   * 1000LL,
                                 AV_TIME_BASE_Q, ist->st->time_base);
        num_rects = sub->num_rects;
    } else {
        pts       = ist->sub2video.end_pts;
        end_pts   = INT64_MAX;
        num_rects = 0;
    }
    if (sub2video_get_blank_frame(ist) < 0) {
        av_log(ist->dec_ctx, AV_LOG_ERROR,
               "Impossible to get a blank canvas.\n");
        return;
    }
    dst          = frame->data    [0];
    dst_linesize = frame->linesize[0];
    for (i = 0; i < num_rects; i++)
        sub2video_copy_rect(dst, dst_linesize, frame->width, frame->height, sub->rects[i]);
    sub2video_push_ref(ist, pts);
    ist->sub2video.end_pts = end_pts;
}

static void sub2video_heartbeat(InputStream *ist, int64_t pts)
{
    InputFile *infile = input_files[ist->file_index];
    int i, j, nb_reqs;
    int64_t pts2;

    /* When a frame is read from a file, examine all sub2video streams in
       the same file and send the sub2video frame again. Otherwise, decoded
       video frames could be accumulating in the filter graph while a filter
       (possibly overlay) is desperately waiting for a subtitle frame. */
    for (i = 0; i < infile->nb_streams; i++) {
        InputStream *ist2 = input_streams[infile->ist_index + i];
        if (!ist2->sub2video.frame)
            continue;
        /* subtitles seem to be usually muxed ahead of other streams;
           if not, subtracting a larger time here is necessary */
        pts2 = av_rescale_q(pts, ist->st->time_base, ist2->st->time_base) - 1;
        /* do not send the heartbeat frame if the subtitle is already ahead */
        if (pts2 <= ist2->sub2video.last_pts)
            continue;
        if (pts2 >= ist2->sub2video.end_pts || !ist2->sub2video.frame->data[0])
            sub2video_update(ist2, NULL);
        for (j = 0, nb_reqs = 0; j < ist2->nb_filters; j++)
            nb_reqs += av_buffersrc_get_nb_failed_requests(ist2->filters[j]->filter);
        if (nb_reqs)
            sub2video_push_ref(ist2, pts2);
    }
}

static void sub2video_flush(InputStream *ist)
{
    int i;

    if (ist->sub2video.end_pts < INT64_MAX)
        sub2video_update(ist, NULL);
    for (i = 0; i < ist->nb_filters; i++)
        av_buffersrc_add_frame(ist->filters[i]->filter, NULL);
}

/* pkt = NULL means EOF (needed to flush decoder buffers) */
int SubtitleInputStream::prepare_packet(const AVPacket *pkt, bool no_eof)
{
   if (pkt)
      sub2video_heartbeat(pkt.pts);

   return InputStream::prepare_packet(pkt, no_eof);
}
