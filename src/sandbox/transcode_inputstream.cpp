#include "transcode_inputstream.h"

extern "C"
{
#include <libavutil/avassert.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
    // #include <libavformat/avformat.h>
}

#include "avexception.h"
#include "transcode_utils.h"
#include "transcode_outputstream.h"
#include "transcode_outputfile.h"
#include "transcode_inputfile.h"
#include "transcode_hw.h"

int exit_on_error = 0;
OutputStream **output_streams = NULL;
int nb_output_streams = 0;
OutputFile **output_files = NULL;
InputStream **input_streams = NULL;
InputFile **input_files = NULL;
static int64_t decode_error_stat[2];

const HWAccel hwaccels[] = {
#if CONFIG_VIDEOTOOLBOX
    { "videotoolbox", videotoolbox_init, HWACCEL_VIDEOTOOLBOX, AV_PIX_FMT_VIDEOTOOLBOX },
#endif
#if CONFIG_LIBMFX
    { "qsv",   qsv_init,   HWACCEL_QSV,   AV_PIX_FMT_QSV },
#endif
#if CONFIG_CUVID
    { "cuvid", cuvid_init, HWACCEL_CUVID, AV_PIX_FMT_CUDA },
#endif
    { 0 },
};


// prototypes
int decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *pkt);
int decode_audio(InputStream *ist, AVPacket *pkt, int *got_output, int *decode_failed);
int decode_video(InputStream *ist, AVPacket *pkt, int *got_output, int64_t *duration_pts, int eof, int *decode_failed);
int transcode_subtitles(InputStream *ist, AVPacket *pkt, int *got_output, int *decode_failed);
int send_filter_eof(InputStream *ist);
void check_decode_result(InputStream *ist, int *got_output, int ret);
int send_frame_to_filters(InputStream *ist, AVFrame *decoded_frame);

void sub2video_flush(InputStream *ist);
void sub2video_push_ref(InputStream *ist, int64_t pts);
int sub2video_get_blank_frame(InputStream *ist);
void sub2video_copy_rect(uint8_t *dst, int dst_linesize, int w, int h, AVSubtitleRect *r);

int init_input_stream(int ist_index, char *error, int error_len)
{
    int ret;
    InputStream *ist = input_streams[ist_index];

    if (ist->decoding_needed)
    {
        AVCodec *codec = ist->dec;
        if (!codec)
        {
            snprintf(error, error_len, "Decoder (codec %s) not found for input stream #%d:%d",
                     avcodec_get_name(ist->dec_ctx->codec_id), ist->file_index, ist->st->index);
            return AVERROR(EINVAL);
        }

        ist->dec_ctx->opaque = ist;
        ist->dec_ctx->get_format = get_format;
        ist->dec_ctx->get_buffer2 = get_buffer;
        ist->dec_ctx->thread_safe_callbacks = 1;

        av_opt_set_int(ist->dec_ctx, "refcounted_frames", 1, 0);
        if (ist->dec_ctx->codec_id == AV_CODEC_ID_DVB_SUBTITLE &&
            (ist->decoding_needed & DECODING_FOR_OST))
        {
            av_dict_set(&ist->decoder_opts, "compute_edt", "1", AV_DICT_DONT_OVERWRITE);
            if (ist->decoding_needed & DECODING_FOR_FILTER)
                AVException::log(AV_LOG_WARNING,
                                 "Warning using DVB subtitles for filtering and output at the same time is not fully supported, also see -compute_edt [0|1]\n");
        }

        av_dict_set(&ist->decoder_opts, "sub_text_format", "ass", AV_DICT_DONT_OVERWRITE);

        /* Useful for subtitles retiming by lavf (FIXME), skipping samples in
         * audio, and video decoders such as cuvid or mediacodec */
        ist->dec_ctx->pkt_timebase = ist->st->time_base;

        if (!av_dict_get(ist->decoder_opts, "threads", NULL, 0))
            av_dict_set(&ist->decoder_opts, "threads", "auto", 0);
        /* Attached pics are sparse, therefore we would not want to delay their decoding till EOF. */
        if (ist->st->disposition & AV_DISPOSITION_ATTACHED_PIC)
            av_dict_set(&ist->decoder_opts, "threads", "1", 0);

        ret = hw_device_setup_for_decode(ist);
        if (ret < 0)
        {
            AVException::sprintf_error(error, error_len, "Device setup failed for "
                                                         "decoder on input stream #%d:%d : %s",
                                       2, ist->file_index, ist->st->index, ret);
            return ret;
        }

        if ((ret = avcodec_open2(ist->dec_ctx, codec, &ist->decoder_opts)) < 0)
        {
            if (ret == AVERROR_EXPERIMENTAL)
                AVException::log_error(AV_LOG_FATAL, "Fatal error: AVERROR_EXPERIMENTAL");

            AVException::sprintf_error(error, error_len,
                                       "Error while opening decoder for input stream "
                                       "#%d:%d : %s",
                                       2, ist->file_index, ist->st->index, ret);
            return ret;
        }
        assert_avoptions(ist->decoder_opts);
    }

    ist->next_pts = AV_NOPTS_VALUE;
    ist->next_dts = AV_NOPTS_VALUE;

    return 0;
}

/* pkt = NULL means EOF (needed to flush decoder buffers) */
int process_input_packet(InputStream *ist, const AVPacket *pkt, int no_eof)
{
    int ret = 0, i;
    int repeating = 0;
    int eof_reached = 0;

    AVPacket avpkt;
    if (!ist->saw_first_ts)
    {
        ist->dts = ist->st->avg_frame_rate.num ? -ist->dec_ctx->has_b_frames * AV_TIME_BASE / av_q2d(ist->st->avg_frame_rate) : 0;
        ist->pts = 0;
        if (pkt && pkt->pts != AV_NOPTS_VALUE && !ist->decoding_needed)
        {
            ist->dts += av_rescale_q(pkt->pts, ist->st->time_base, AV_TIME_BASE_Q);
            ist->pts = ist->dts; //unused but better to set it to a value thats not totally wrong
        }
        ist->saw_first_ts = 1;
    }

    if (ist->next_dts == AV_NOPTS_VALUE)
        ist->next_dts = ist->dts;
    if (ist->next_pts == AV_NOPTS_VALUE)
        ist->next_pts = ist->pts;

    if (!pkt)
    {
        /* EOF handling */
        av_init_packet(&avpkt);
        avpkt.data = NULL;
        avpkt.size = 0;
    }
    else
    {
        avpkt = *pkt;
    }

    if (pkt && pkt->dts != AV_NOPTS_VALUE)
    {
        ist->next_dts = ist->dts = av_rescale_q(pkt->dts, ist->st->time_base, AV_TIME_BASE_Q);
        if (ist->dec_ctx->codec_type != AVMEDIA_TYPE_VIDEO || !ist->decoding_needed)
            ist->next_pts = ist->pts = ist->dts;
    }

    // while we have more to decode or while the decoder did output something on EOF
    while (ist->decoding_needed)
    {
        int64_t duration_dts = 0;
        int64_t duration_pts = 0;
        int got_output = 0;
        int decode_failed = 0;

        ist->pts = ist->next_pts;
        ist->dts = ist->next_dts;

        switch (ist->dec_ctx->codec_type)
        {
        case AVMEDIA_TYPE_AUDIO:
            ret = decode_audio(ist, repeating ? NULL : &avpkt, &got_output,
                               &decode_failed);
            break;
        case AVMEDIA_TYPE_VIDEO:
            ret = decode_video(ist, repeating ? NULL : &avpkt, &got_output, &duration_pts, !pkt,
                               &decode_failed);
            if (!repeating || !pkt || got_output)
            {
                if (pkt && pkt->duration)
                {
                    duration_dts = av_rescale_q(pkt->duration, ist->st->time_base, AV_TIME_BASE_Q);
                }
                else if (ist->dec_ctx->framerate.num != 0 && ist->dec_ctx->framerate.den != 0)
                {
                    int ticks = av_stream_get_parser(ist->st) ? av_stream_get_parser(ist->st)->repeat_pict + 1 : ist->dec_ctx->ticks_per_frame;
                    duration_dts = ((int64_t)AV_TIME_BASE *
                                    ist->dec_ctx->framerate.den * ticks) /
                                   ist->dec_ctx->framerate.num / ist->dec_ctx->ticks_per_frame;
                }

                if (ist->dts != AV_NOPTS_VALUE && duration_dts)
                {
                    ist->next_dts += duration_dts;
                }
                else
                    ist->next_dts = AV_NOPTS_VALUE;
            }

            if (got_output)
            {
                if (duration_pts > 0)
                {
                    ist->next_pts += av_rescale_q(duration_pts, ist->st->time_base, AV_TIME_BASE_Q);
                }
                else
                {
                    ist->next_pts += duration_dts;
                }
            }
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            if (repeating)
                break;
            ret = transcode_subtitles(ist, &avpkt, &got_output, &decode_failed);
            if (!pkt && ret >= 0)
                ret = AVERROR_EOF;
            break;
        default:
            return -1;
        }

        if (ret == AVERROR_EOF)
        {
            eof_reached = 1;
            break;
        }

        if (ret < 0)
        {
            if (decode_failed)
            {
                AVException::log_error(AV_LOG_ERROR, "Error while decoding stream #%d:%d: %s\n",
                                       2, ist->file_index, ist->st->index, ret);
            }
            else
            {
                av_log(NULL, AV_LOG_FATAL, "Error while processing the decoded "
                                           "data for stream #%d:%d\n",
                       ist->file_index, ist->st->index);
            }
            if (!decode_failed || exit_on_error)
                AVException::log_error(AV_LOG_FATAL, "Failed to decode a packet.");
            break;
        }

        if (got_output)
            ist->got_output = 1;

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

        repeating = 1;
    }

    /* after flushing, send an EOF on all the filter inputs attached to the stream */
    /* except when looping we need to flush but not to send an EOF */
    if (!pkt && ist->decoding_needed && eof_reached && !no_eof)
    {
        int ret = send_filter_eof(ist);
        if (ret < 0)
            AVException::log_error(AV_LOG_FATAL, "Error marking filters as finished\n");
    }

    /* handle stream copy */
    // if (!ist->decoding_needed && pkt)
    // {
    //     ist->dts = ist->next_dts;
    //     switch (ist->dec_ctx->codec_type)
    //     {
    //     case AVMEDIA_TYPE_AUDIO:
    //         av_assert1(pkt->duration >= 0);
    //         if (ist->dec_ctx->sample_rate)
    //         {
    //             ist->next_dts += ((int64_t)AV_TIME_BASE * ist->dec_ctx->frame_size) /
    //                              ist->dec_ctx->sample_rate;
    //         }
    //         else
    //         {
    //             ist->next_dts += av_rescale_q(pkt->duration, ist->st->time_base, AV_TIME_BASE_Q);
    //         }
    //         break;
    //     case AVMEDIA_TYPE_VIDEO:
    //         if (ist->framerate.num)
    //         {
    //             // TODO: Remove work-around for c99-to-c89 issue 7
    //             AVRational time_base_q = AV_TIME_BASE_Q;
    //             int64_t next_dts = av_rescale_q(ist->next_dts, time_base_q, av_inv_q(ist->framerate));
    //             ist->next_dts = av_rescale_q(next_dts + 1, av_inv_q(ist->framerate), time_base_q);
    //         }
    //         else if (pkt->duration)
    //         {
    //             ist->next_dts += av_rescale_q(pkt->duration, ist->st->time_base, AV_TIME_BASE_Q);
    //         }
    //         else if (ist->dec_ctx->framerate.num != 0)
    //         {
    //             int ticks = av_stream_get_parser(ist->st) ? av_stream_get_parser(ist->st)->repeat_pict + 1 : ist->dec_ctx->ticks_per_frame;
    //             ist->next_dts += ((int64_t)AV_TIME_BASE *
    //                               ist->dec_ctx->framerate.den * ticks) /
    //                              ist->dec_ctx->framerate.num / ist->dec_ctx->ticks_per_frame;
    //         }
    //         break;
    //     }
    //     ist->pts = ist->dts;
    //     ist->next_pts = ist->next_dts;
    // }
    // for (i = 0; i < nb_output_streams; i++)
    // {
    //     OutputStream *ost = output_streams[i];

    //     if (!check_output_constraints(ist, ost) || ost->encoding_needed)
    //         continue;

    //     do_streamcopy(ist, ost, pkt);
    // }

    return !eof_reached;
}

int decode_audio(InputStream *ist, AVPacket *pkt, int *got_output, int *decode_failed)
{
    AVFrame *decoded_frame;
    AVCodecContext *avctx = ist->dec_ctx;
    int ret, err = 0;
    AVRational decoded_frame_tb;

    if (!ist->decoded_frame && !(ist->decoded_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    if (!ist->filter_frame && !(ist->filter_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    decoded_frame = ist->decoded_frame;

    // update_benchmark(NULL);
    ret = decode(avctx, decoded_frame, got_output, pkt);
    // update_benchmark("decode_audio %d.%d", ist->file_index, ist->st->index);
    if (ret < 0)
        *decode_failed = 1;

    if (ret >= 0 && avctx->sample_rate <= 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Sample rate %d invalid\n", avctx->sample_rate);
        ret = AVERROR_INVALIDDATA;
    }

    if (ret != AVERROR_EOF)
        check_decode_result(ist, got_output, ret);

    if (!*got_output || ret < 0)
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
                                              AVRational({1, avctx->sample_rate}), decoded_frame->nb_samples, &ist->filter_in_rescale_delta_last,
                                              AVRational({1, avctx->sample_rate}));
    ist->nb_samples = decoded_frame->nb_samples;
    err = send_frame_to_filters(ist, decoded_frame);

    av_frame_unref(ist->filter_frame);
    av_frame_unref(decoded_frame);
    return err < 0 ? err : ret;
}

int decode_video(InputStream *ist, AVPacket *pkt, int *got_output, int64_t *duration_pts, int eof, int *decode_failed)
{
    AVFrame *decoded_frame;
    int i, ret = 0, err = 0;
    int64_t best_effort_timestamp;
    int64_t dts = AV_NOPTS_VALUE;
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
        void *new_array = av_realloc_array(ist->dts_buffer, ist->nb_dts_buffer + 1, sizeof(ist->dts_buffer[0]));
        if (!new_array)
            return AVERROR(ENOMEM);
        ist->dts_buffer = (int64_t *)new_array;
        ist->dts_buffer[ist->nb_dts_buffer++] = dts;
    }

    // update_benchmark(NULL);
    ret = decode(ist->dec_ctx, decoded_frame, got_output, pkt ? &avpkt : NULL);
    // update_benchmark("decode_video %d.%d", ist->file_index, ist->st->index);
    if (ret < 0)
        *decode_failed = 1;

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
                   "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)\n",
                   ist->dec_ctx->has_b_frames,
                   ist->st->codecpar->video_delay);
    }

    if (ret != AVERROR_EOF)
        check_decode_result(ist, got_output, ret);

    if (*got_output && ret >= 0)
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

    if (!*got_output || ret < 0)
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
    ist->hwaccel_retrieved_pix_fmt = AVPixelFormat(decoded_frame->format);

    best_effort_timestamp = decoded_frame->best_effort_timestamp;
    *duration_pts = decoded_frame->pkt_duration;

    if (ist->framerate.num)
        best_effort_timestamp = ist->cfr_next_pts++;

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

    // if (debug_ts)
    // {
    //     av_log(NULL, AV_LOG_INFO, "decoder -> ist_index:%d type:video "
    //                               "frame_pts:%s frame_pts_time:%s best_effort_ts:%" PRId64 " best_effort_ts_time:%s keyframe:%d frame_type:%d time_base:%d/%d\n",
    //            ist->st->index, av_ts2str(decoded_frame->pts),
    //            av_ts2timestr(decoded_frame->pts, &ist->st->time_base),
    //            best_effort_timestamp,
    //            av_ts2timestr(best_effort_timestamp, &ist->st->time_base),
    //            decoded_frame->key_frame, decoded_frame->pict_type,
    //            ist->st->time_base.num, ist->st->time_base.den);
    // }

    if (ist->st->sample_aspect_ratio.num)
        decoded_frame->sample_aspect_ratio = ist->st->sample_aspect_ratio;

    err = send_frame_to_filters(ist, decoded_frame);

fail:
    av_frame_unref(ist->filter_frame);
    av_frame_unref(decoded_frame);
    return err < 0 ? err : ret;
}

int transcode_subtitles(InputStream *ist, AVPacket *pkt, int *got_output, int *decode_failed)
{
    AVSubtitle subtitle;
    int free_sub = 1;
    int i, ret = avcodec_decode_subtitle2(ist->dec_ctx,
                                          &subtitle, got_output, pkt);

    check_decode_result(NULL, got_output, ret);

    if (ret < 0 || !*got_output)
    {
        *decode_failed = 1;
        if (!pkt->size)
            sub2video_flush(ist);
        return ret;
    }

    if (ist->fix_sub_duration)
    {
        int end = 1;
        if (ist->prev_sub.got_output)
        {
            end = av_rescale(subtitle.pts - ist->prev_sub.subtitle.pts,
                             1000, AV_TIME_BASE);
            if (end < ist->prev_sub.subtitle.end_display_time)
            {
                av_log(ist->dec_ctx, AV_LOG_DEBUG,
                       "Subtitle duration reduced from %" PRId32 " to %d%s\n",
                       ist->prev_sub.subtitle.end_display_time, end,
                       end <= 0 ? ", dropping it" : "");
                ist->prev_sub.subtitle.end_display_time = end;
            }
        }
        FFSWAP(int, *got_output, ist->prev_sub.got_output);
        FFSWAP(int, ret, ist->prev_sub.ret);
        FFSWAP(AVSubtitle, subtitle, ist->prev_sub.subtitle);
        if (end <= 0)
            goto out;
    }

    if (!*got_output)
        return ret;

    if (ist->sub2video.frame)
    {
        sub2video_update(ist, &subtitle);
    }
    else if (ist->filters.size())
    {
        if (!ist->sub2video.sub_queue)
            ist->sub2video.sub_queue = av_fifo_alloc(8 * sizeof(AVSubtitle));
        if (!ist->sub2video.sub_queue)
            AVException::log_error(AV_LOG_FATAL, "No subtitle queue is found.");
        if (!av_fifo_space(ist->sub2video.sub_queue))
        {
            ret = av_fifo_realloc2(ist->sub2video.sub_queue, 2 * av_fifo_size(ist->sub2video.sub_queue));
            if (ret < 0)
                AVException::log_error(AV_LOG_FATAL, "Failed to allocate FIFO buffer for subtitle queue.");
        }
        av_fifo_generic_write(ist->sub2video.sub_queue, &subtitle, sizeof(subtitle), NULL);
        free_sub = 0;
    }

    if (!subtitle.num_rects)
        goto out;

    ist->frames_decoded++;

    for (i = 0; i < nb_output_streams; i++)
    {
        OutputStream *ost = output_streams[i];

        // if (!check_output_constraints(ist, ost) || !ost->encoding_needed || ost->enc->type != AVMEDIA_TYPE_SUBTITLE)
        //     continue;

        do_subtitle_out(output_files[ost->file_index], ost, &subtitle);
    }

out:
    if (free_sub)
        avsubtitle_free(&subtitle);
    return ret;
}

int send_filter_eof(InputStream *ist)
{
    int i, ret;
    /* TODO keep pts also in stream time base to avoid converting back */
    int64_t pts = av_rescale_q_rnd(ist->pts, AV_TIME_BASE_Q, ist->st->time_base,
                                   AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

    for (i = 0; i < ist->filters.size(); i++)
    {
        ret = ifilter_send_eof(ist->filters[i], pts);
        if (ret < 0)
            return ret;
    }
    return 0;
}

void sub2video_heartbeat(InputStream *ist, int64_t pts)
{
    InputFile *infile = input_files[ist->file_index];
    int i, j, nb_reqs;
    int64_t pts2;

    /* When a frame is read from a file, examine all sub2video streams in
       the same file and send the sub2video frame again. Otherwise, decoded
       video frames could be accumulating in the filter graph while a filter
       (possibly overlay) is desperately waiting for a subtitle frame. */
    for (i = 0; i < infile->nb_streams; i++)
    {
        InputStream *ist2 = input_streams[infile->ist_index + i];
        if (!ist2->sub2video.frame)
            continue;
        /* subtitles seem to be usually muxed ahead of other streams;
           if not, subtracting a larger time here is necessary */
        pts2 = av_rescale_q(pts, ist->st->time_base, ist2->st->time_base) - 1;
        /* do not send the heartbeat frame if the subtitle is already ahead */
        if (pts2 <= ist2->sub2video.last_pts)
            continue;
        if (pts2 >= ist2->sub2video.end_pts ||
            (!ist2->sub2video.frame->data[0] && ist2->sub2video.end_pts < INT64_MAX))
            sub2video_update(ist2, NULL);
        for (j = 0, nb_reqs = 0; j < ist2->filters.size(); j++)
            nb_reqs += av_buffersrc_get_nb_failed_requests(ist2->filters[j]->filter);
        if (nb_reqs)
            sub2video_push_ref(ist2, pts2);
    }
}

void check_decode_result(InputStream *ist, int *got_output, int ret)
{
    if (*got_output || ret < 0)
        decode_error_stat[ret < 0]++;

    if (ret < 0 && exit_on_error)
        AVException::log_error(AV_LOG_FATAL, "Decoding did not complete successfully.");

    if (*got_output && ist)
    {
        if (ist->decoded_frame->decode_error_flags || (ist->decoded_frame->flags & AV_FRAME_FLAG_CORRUPT))
        {
            AVException::log(exit_on_error ? AV_LOG_FATAL : AV_LOG_WARNING,
                             "%s: corrupt decoded frame in stream %d\n", input_files[ist->file_index]->ctx->url, ist->st->index);
            if (exit_on_error)
                throw; // just in case
        }
    }
}

int send_frame_to_filters(InputStream *ist, AVFrame *decoded_frame)
{
    int i, ret;
    AVFrame *f;

    av_assert1(ist->filters.size() > 0); /* ensure ret is initialized */
    for (i = 0; i < ist->filters.size(); i++)
    {
        if (i < ist->filters.size() - 1)
        {
            f = ist->filter_frame;
            ret = av_frame_ref(f, decoded_frame);
            if (ret < 0)
                break;
        }
        else
            f = decoded_frame;
        ret = ifilter_send_frame(ist->filters[i], f);
        if (ret == AVERROR_EOF)
            ret = 0; /* ignore */
        if (ret < 0)
        {
            AVException::log_error(AV_LOG_ERROR,
                                   "Failed to inject frame into filter network: %s\n", ret);
            break;
        }
    }
    return ret;
}

void sub2video_flush(InputStream *ist)
{
    int i;
    int ret;

    if (ist->sub2video.end_pts < INT64_MAX)
        sub2video_update(ist, NULL);
    for (i = 0; i < ist->filters.size(); i++)
    {
        ret = av_buffersrc_add_frame(ist->filters[i]->filter, NULL);
        if (ret != AVERROR_EOF && ret < 0)
            av_log(NULL, AV_LOG_WARNING, "Flush the frame error.\n");
    }
}

void sub2video_push_ref(InputStream *ist, int64_t pts)
{
    AVFrame *frame = ist->sub2video.frame;
    int i;
    int ret;

    av_assert1(frame->data[0]);
    ist->sub2video.last_pts = frame->pts = pts;
    for (i = 0; i < ist->filters.size(); i++)
    {
        ret = av_buffersrc_add_frame_flags(ist->filters[i]->filter, frame,
                                           AV_BUFFERSRC_FLAG_KEEP_REF |
                                               AV_BUFFERSRC_FLAG_PUSH);
        if (ret != AVERROR_EOF && ret < 0)
            AVException::log_error(AV_LOG_WARNING, "Error while add the frame to buffer source(%s).\n",
                                   ret);
    }
}

void sub2video_update(InputStream *ist, AVSubtitle *sub)
{
    AVFrame *frame = ist->sub2video.frame;
    int8_t *dst;
    int dst_linesize;
    int num_rects, i;
    int64_t pts, end_pts;

    if (!frame)
        return;
    if (sub)
    {
        pts = av_rescale_q(sub->pts + sub->start_display_time * 1000LL,
                           AV_TIME_BASE_Q, ist->st->time_base);
        end_pts = av_rescale_q(sub->pts + sub->end_display_time * 1000LL,
                               AV_TIME_BASE_Q, ist->st->time_base);
        num_rects = sub->num_rects;
    }
    else
    {
        pts = ist->sub2video.end_pts;
        end_pts = INT64_MAX;
        num_rects = 0;
    }
    if (sub2video_get_blank_frame(ist) < 0)
    {
        av_log(ist->dec_ctx, AV_LOG_ERROR,
               "Impossible to get a blank canvas.\n");
        return;
    }
    dst = (int8_t*)frame->data[0];
    dst_linesize = frame->linesize[0];
    for (i = 0; i < num_rects; i++)
        sub2video_copy_rect((uint8_t*)dst, dst_linesize, frame->width, frame->height, sub->rects[i]);
    sub2video_push_ref(ist, pts);
    ist->sub2video.end_pts = end_pts;
}

/////////////

enum AVPixelFormat get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts)
{
    InputStream *ist = (InputStream *)s->opaque;
    const enum AVPixelFormat *p;
    int ret;

    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
    {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        const AVCodecHWConfig *config = NULL;
        int i;

        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            break;

        if (ist->hwaccel_id == HWACCEL_GENERIC ||
            ist->hwaccel_id == HWACCEL_AUTO)
        {
            for (i = 0;; i++)
            {
                config = avcodec_get_hw_config(s->codec, i);
                if (!config)
                    break;
                if (!(config->methods &
                      AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
                    continue;
                if (config->pix_fmt == *p)
                    break;
            }
        }
        if (config)
        {
            if (config->device_type != ist->hwaccel_device_type)
            {
                // Different hwaccel offered, ignore.
                continue;
            }

            ret = hwaccel_decode_init(s);
            if (ret < 0)
            {
                if (ist->hwaccel_id == HWACCEL_GENERIC)
                {
                    av_log(NULL, AV_LOG_FATAL,
                           "%s hwaccel requested for input stream #%d:%d, "
                           "but cannot be initialized.\n",
                           av_hwdevice_get_type_name(config->device_type),
                           ist->file_index, ist->st->index);
                    return AV_PIX_FMT_NONE;
                }
                continue;
            }
        }
        else
        {
            const HWAccel *hwaccel = NULL;
            int i;
            for (i = 0; hwaccels[i].name; i++)
            {
                if (hwaccels[i].pix_fmt == *p)
                {
                    hwaccel = &hwaccels[i];
                    break;
                }
            }
            if (!hwaccel)
            {
                // No hwaccel supporting this pixfmt.
                continue;
            }
            if (hwaccel->id != ist->hwaccel_id)
            {
                // Does not match requested hwaccel.
                continue;
            }

            ret = hwaccel->init(s);
            if (ret < 0)
            {
                av_log(NULL, AV_LOG_FATAL,
                       "%s hwaccel requested for input stream #%d:%d, "
                       "but cannot be initialized.\n",
                       hwaccel->name,
                       ist->file_index, ist->st->index);
                return AV_PIX_FMT_NONE;
            }
        }

        if (ist->hw_frames_ctx)
        {
            s->hw_frames_ctx = av_buffer_ref(ist->hw_frames_ctx);
            if (!s->hw_frames_ctx)
                return AV_PIX_FMT_NONE;
        }

        ist->hwaccel_pix_fmt = *p;
        break;
    }

    return *p;
}

int get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    InputStream *ist = (InputStream *)s->opaque;

    if (ist->hwaccel_get_buffer && frame->format == ist->hwaccel_pix_fmt)
        return ist->hwaccel_get_buffer(s, frame, flags);

    return avcodec_default_get_buffer2(s, frame, flags);
}

// This does not quite work like avcodec_decode_audio4/avcodec_decode_video2.
// There is the following difference: if you got a frame, you must call
// it again with pkt=NULL. pkt==NULL is treated differently from pkt->size==0
// (pkt==NULL means get more output, pkt->size==0 is a flush/drain packet)
int decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *pkt)
{
    int ret;

    *got_frame = 0;

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
    if (ret >= 0)
        *got_frame = 1;

    return 0;
}

int sub2video_get_blank_frame(InputStream *ist)
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

void sub2video_copy_rect(uint8_t *dst, int dst_linesize, int w, int h, AVSubtitleRect *r)
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
