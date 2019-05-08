#include "transcode_outputfile.h"

#include <atomic>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
#include <libavutil/intreadwrite.h>
#include <libavfilter/avfilter.h>
#include <libavutil/avassert.h>
#include <libavutil/opt.h>
#include <libavutil/avstring.h>
}

#include "avexception.h"
#include "transcode_utils.h"
#include "transcode_inputstream.h"
#include "transcode_inputfile.h"

OutputStream **output_streams = NULL;
int nb_output_streams = 0;
OutputFile **output_files = NULL;
int nb_output_files = 0;
InputStream **input_streams = NULL;
InputFile **input_files = NULL;

#define VSYNC_AUTO -1
#define VSYNC_PASSTHROUGH 0
#define VSYNC_CFR 1
#define VSYNC_VFR 2
#define VSYNC_VSCFR 0xfe
#define VSYNC_DROP 0xff
int video_sync_method = VSYNC_AUTO;
int audio_sync_method = 0;
int main_return_code = 0;
int exit_on_error = 0;
int copy_ts = 0;
float frame_drop_threshold = 0;
int nb_frames_drop = 0;
int nb_frames_dup = 0;
unsigned dup_warning = 1000;
float dts_error_threshold = 3600 * 30;
uint8_t *subtitle_out;
std::atomic_int transcode_init_done = 0;
static volatile int received_nb_signals = 0;

int decode_interrupt_cb(void *ctx);

const AVIOInterruptCB int_cb = {decode_interrupt_cb, NULL};

// output file
void write_packet(OutputFile *of, AVPacket *pkt, OutputStream *ost, int unqueue);
void close_all_output_streams(OutputStream *ost, OSTFinished this_stream, OSTFinished others);

/* open the muxer when all the streams are initialized */
int check_init_output_file(OutputFile *of, int file_index)
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
        AVException::log_error(AV_LOG_ERROR,
                               "Could not write header for output file #%d "
                               "(incorrect codec parameters ?): %s\n",
                               1, file_index, ret);
        return ret;
    }
    //assert_avoptions(of->opts);
    of->header_written = 1;

    av_dump_format(of->ctx, file_index, of->ctx->url, 1);

    // if (sdp_filename || want_sdp)
    //     print_sdp();

    /* flush the muxing queues */
    for (i = 0; i < of->ctx->nb_streams; i++)
    {
        OutputStream *ost = output_streams[of->ost_index + i];

        /* try to improve muxing time_base (only possible if nothing has been written yet) */
        if (!av_fifo_size(ost->muxing_queue))
            ost->mux_timebase = ost->st->time_base;

        while (av_fifo_size(ost->muxing_queue))
        {
            AVPacket pkt;
            av_fifo_generic_read(ost->muxing_queue, &pkt, sizeof(pkt), NULL);
            write_packet(of, &pkt, ost, 1);
        }
    }

    return 0;
}

OutputStream *choose_output(void)
{
    int i;
    int64_t opts_min = INT64_MAX;
    OutputStream *ost_min = NULL;

    for (i = 0; i < nb_output_streams; i++)
    {
        OutputStream *ost = output_streams[i];
        int64_t opts = ost->st->cur_dts == AV_NOPTS_VALUE ? INT64_MIN : av_rescale_q(ost->st->cur_dts, ost->st->time_base, AV_TIME_BASE_Q);
        if (ost->st->cur_dts == AV_NOPTS_VALUE)
            av_log(NULL, AV_LOG_DEBUG, "cur_dts is invalid (this is harmless if it occurs once at the start per stream)\n");

        if (!ost->initialized && !ost->inputs_done)
            return ost;

        if (!ost->finished && opts < opts_min)
        {
            opts_min = opts;
            ost_min = ost->unavailable ? NULL : ost;
        }
    }
    return ost_min;
}

/*
 * Send a single packet to the output, applying any bitstream filters
 * associated with the output stream.  This may result in any number
 * of packets actually being written, depending on what bitstream
 * filters are applied.  The supplied packet is consumed and will be
 * blank (as if newly-allocated) when this function returns.
 *
 * If eof is set, instead indicate EOF to all bitstream filters and
 * therefore flush any delayed packets to the output.  A blank packet
 * must be supplied in this case.
 */
void output_packet(OutputFile *of, AVPacket *pkt, OutputStream *ost, int eof)
{
    int ret = 0;

    /* apply the output bitstream filters, if any */
    if (ost->nb_bitstream_filters)
    {
        int idx;

        ret = av_bsf_send_packet(ost->bsf_ctx[0], eof ? NULL : pkt);
        if (ret < 0)
            goto finish;

        eof = 0;
        idx = 1;
        while (idx)
        {
            /* get a packet from the previous filter up the chain */
            ret = av_bsf_receive_packet(ost->bsf_ctx[idx - 1], pkt);
            if (ret == AVERROR(EAGAIN))
            {
                ret = 0;
                idx--;
                continue;
            }
            else if (ret == AVERROR_EOF)
            {
                eof = 1;
            }
            else if (ret < 0)
                goto finish;

            /* send it to the next filter down the chain or to the muxer */
            if (idx < ost->nb_bitstream_filters)
            {
                ret = av_bsf_send_packet(ost->bsf_ctx[idx], eof ? NULL : pkt);
                if (ret < 0)
                    goto finish;
                idx++;
                eof = 0;
            }
            else if (eof)
                goto finish;
            else
                write_packet(of, pkt, ost, 0);
        }
    }
    else if (!eof)
        write_packet(of, pkt, ost, 0);

finish:
    if (ret < 0 && ret != AVERROR_EOF)
    {
        AVException::log(AV_LOG_ERROR, "Error applying bitstream filters to an output "
                                       "packet for stream #%d:%d.\n",
                         ost->file_index, ost->index);
        if (exit_on_error)
            throw;
    }
}

void write_packet(OutputFile *of, AVPacket *pkt, OutputStream *ost, int unqueue)
{
    AVFormatContext *s = of->ctx;
    AVStream *st = ost->st;
    int ret;

    /*
     * Audio encoders may split the packets --  #frames in != #packets out.
     * But there is no reordering, so we can limit the number of output packets
     * by simply dropping them here.
     * Counting encoded video frames needs to be done separately because of
     * reordering, see do_video_out().
     * Do not count the packet when unqueued because it has been counted when queued.
     */
    if (!(st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && ost->encoding_needed) && !unqueue)
    {
        if (ost->frame_number >= ost->max_frames)
        {
            av_packet_unref(pkt);
            return;
        }
        ost->frame_number++;
    }

    if (!of->header_written)
    {
        AVPacket tmp_pkt = {0};
        /* the muxer is not initialized yet, buffer the packet */
        if (!av_fifo_space(ost->muxing_queue))
        {
            int new_size = FFMIN(2 * av_fifo_size(ost->muxing_queue),
                                 ost->max_muxing_queue_size);
            if (new_size <= av_fifo_size(ost->muxing_queue))
            {
                AVException::log(AV_LOG_FATAL,
                                 "Too many packets buffered for output stream %d:%d.\n",
                                 ost->file_index, ost->st->index);
                throw;
            }
            ret = av_fifo_realloc2(ost->muxing_queue, new_size);
            if (ret < 0)
            {
                AVException::log(AV_LOG_FATAL, "Failed to allocate FIFO for mux queue.");
                throw;
            }
        }
        ret = av_packet_make_refcounted(pkt);
        if (ret < 0)
        {
            AVException::log(AV_LOG_FATAL, "Failed to ensure the data described by a given packet is reference counted.");
            throw;
        }
        av_packet_move_ref(&tmp_pkt, pkt);
        av_fifo_generic_write(ost->muxing_queue, &tmp_pkt, sizeof(tmp_pkt), NULL);
        return;
    }

    if ((st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_sync_method == VSYNC_DROP) ||
        (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_sync_method < 0))
        pkt->pts = pkt->dts = AV_NOPTS_VALUE;

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
                                         ost->mux_timebase);
        }
    }

    av_packet_rescale_ts(pkt, ost->mux_timebase, ost->st->time_base);

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
        if ((st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO || st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO || st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) &&
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
                    AVException::log(AV_LOG_FATAL, "aborting.\n");
                    throw;
                }
                AVException::log(s, loglevel, "changing to %" PRId64 ". This may result "
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

    // if (debug_ts)
    // {
    //     av_log(NULL, AV_LOG_INFO, "muxer <- type:%s "
    //                               "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s size:%d\n",
    //            av_get_media_type_string(ost->enc_ctx->codec_type),
    //            av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &ost->st->time_base),
    //            av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &ost->st->time_base),
    //            pkt->size);
    // }

    ret = av_interleaved_write_frame(s, pkt);
    if (ret < 0)
    {
        AVException::log_error(AV_LOG_ERROR, "av_interleaved_write_frame(): %s", ret);
        main_return_code = 1;
        close_all_output_streams(ost, OSTFinished(MUXER_FINISHED | ENCODER_FINISHED), OSTFinished(ENCODER_FINISHED));
    }
    av_packet_unref(pkt);
}

void do_video_out(OutputFile *of, OutputStream *ost, AVFrame *next_picture, double sync_ipts)
{
    int ret, format_video_sync;
    AVPacket pkt;
    AVCodecContext *enc = ost->enc_ctx;
    AVCodecParameters *mux_par = ost->st->codecpar;
    AVRational frame_rate;
    int nb_frames, nb0_frames, i;
    double delta, delta0;
    double duration = 0;
    int frame_size = 0;
    InputStream *ist = NULL;
    AVFilterContext *filter = ost->filter->filter;

    if (ost->source_index >= 0)
        ist = input_streams[ost->source_index];

    frame_rate = av_buffersink_get_frame_rate(filter);
    if (frame_rate.num > 0 && frame_rate.den > 0)
        duration = 1 / (av_q2d(frame_rate) * av_q2d(enc->time_base));

    if (ist && ist->st->start_time != AV_NOPTS_VALUE && ist->st->first_dts != AV_NOPTS_VALUE && ost->frame_rate.num)
        duration = FFMIN(duration, 1 / (av_q2d(ost->frame_rate) * av_q2d(enc->time_base)));

    if (!ost->filters_script &&
        !ost->filters &&
        next_picture &&
        ist &&
        lrintf(next_picture->pkt_duration * av_q2d(ist->st->time_base) / av_q2d(enc->time_base)) > 0)
    {
        duration = lrintf(next_picture->pkt_duration * av_q2d(ist->st->time_base) / av_q2d(enc->time_base));
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
                av_log(NULL, AV_LOG_VERBOSE, "Past duration %f too large\n", -delta0);
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
        if (nb_frames_dup > dup_warning)
        {
            av_log(NULL, AV_LOG_WARNING, "More than %d frames duplicated\n", dup_warning);
            dup_warning *= 10;
        }
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

        // #if 1
        if (!check_recording_time(ost))
            // #else
            // if (ost->frame_number >= ost->max_frames)
            // #endif
            return;

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
            in_picture->pict_type = AVPictureType(0);

            if (ost->forced_kf_ref_pts == AV_NOPTS_VALUE &&
                in_picture->pts != AV_NOPTS_VALUE)
                ost->forced_kf_ref_pts = in_picture->pts;

            pts_time = in_picture->pts != AV_NOPTS_VALUE ? (in_picture->pts - ost->forced_kf_ref_pts) * av_q2d(enc->time_base) : NAN;
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
                // ff_dlog(NULL, "force_key_frame: n:%f n_forced:%f prev_forced_n:%f t:%f prev_forced_t:%f -> res:%f\n",
                //         ost->forced_keyframes_expr_const_values[FKF_N],
                //         ost->forced_keyframes_expr_const_values[FKF_N_FORCED],
                //         ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_N],
                //         ost->forced_keyframes_expr_const_values[FKF_T],
                //         ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_T],
                //         res);
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

            // update_benchmark(NULL);
            // if (debug_ts)
            // {
            //     av_log(NULL, AV_LOG_INFO, "encoder <- type:video "
            //                               "frame_pts:%s frame_pts_time:%s time_base:%d/%d\n",
            //            av_ts2str(in_picture->pts), av_ts2timestr(in_picture->pts, &enc->time_base),
            //            enc->time_base.num, enc->time_base.den);
            // }

            ost->frames_encoded++;

            ret = avcodec_send_frame(enc, in_picture);
            if (ret < 0)
                goto error;

            while (1)
            {
                ret = avcodec_receive_packet(enc, &pkt);
                // update_benchmark("encode_video %d.%d", ost->file_index, ost->index);
                if (ret == AVERROR(EAGAIN))
                    break;
                if (ret < 0)
                    goto error;

                // if (debug_ts)
                // {
                //     av_log(NULL, AV_LOG_INFO, "encoder -> type:video "
                //                               "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s\n",
                //            av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &enc->time_base),
                //            av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &enc->time_base));
                // }

                if (pkt.pts == AV_NOPTS_VALUE && !(enc->codec->capabilities & AV_CODEC_CAP_DELAY))
                    pkt.pts = ost->sync_opts;

                av_packet_rescale_ts(&pkt, enc->time_base, ost->mux_timebase);

                // if (debug_ts)
                // {
                //     av_log(NULL, AV_LOG_INFO, "encoder -> type:video "
                //                               "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s\n",
                //            av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &ost->mux_timebase),
                //            av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &ost->mux_timebase));
                // }

                frame_size = pkt.size;
                output_packet(of, &pkt, ost, 0);

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

        // if (vstats_filename && frame_size)
        //     do_video_stats(ost, frame_size);
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
    AVException::log(AV_LOG_FATAL, "Video encoding failed\n");
    throw;
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
    // update_benchmark(NULL);
    // if (debug_ts)
    // {
    //     av_log(NULL, AV_LOG_INFO, "encoder <- type:audio "
    //                               "frame_pts:%s frame_pts_time:%s time_base:%d/%d\n",
    //            av_ts2str(frame->pts), av_ts2timestr(frame->pts, &enc->time_base),
    //            enc->time_base.num, enc->time_base.den);
    // }

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

        // update_benchmark("encode_audio %d.%d", ost->file_index, ost->index);

        av_packet_rescale_ts(&pkt, enc->time_base, ost->mux_timebase);

        // if (debug_ts)
        // {
        //     av_log(NULL, AV_LOG_INFO, "encoder -> type:audio "
        //                               "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s\n",
        //            av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &enc->time_base),
        //            av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &enc->time_base));
        // }

        output_packet(of, &pkt, ost, 0);
    }

    return;
error:
    AVException::log(AV_LOG_FATAL, "Audio encoding failed\n");
    throw;
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
        AVException::log(exit_on_error ? AV_LOG_FATAL : AV_LOG_ERROR, "Subtitle packets must have a pts\n");
        if (exit_on_error)
            throw;
        return;
    }

    enc = ost->enc_ctx;

    if (!subtitle_out)
    {
        subtitle_out = (uint8_t *)av_malloc(subtitle_out_max_size);
        if (!subtitle_out)
        {
            AVException::log(AV_LOG_FATAL, "Failed to allocate subtitle_out\n");
            throw;
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
        sub->pts += av_rescale_q(sub->start_display_time, AVRational({1, 1000}), AV_TIME_BASE_Q);
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
            AVException::log(AV_LOG_FATAL, "Subtitle encoding failed\n");
            throw;
        }

        av_init_packet(&pkt);
        pkt.data = subtitle_out;
        pkt.size = subtitle_out_size;
        pkt.pts = av_rescale_q(sub->pts, AV_TIME_BASE_Q, ost->mux_timebase);
        pkt.duration = av_rescale_q(sub->end_display_time, AVRational({1, 1000}), ost->mux_timebase);
        if (enc->codec_id == AV_CODEC_ID_DVB_SUBTITLE)
        {
            /* XXX: the pts correction is handled here. Maybe handling
               it in the codec would be better */
            if (i == 0)
                pkt.pts += av_rescale_q(sub->start_display_time, AVRational({1, 1000}), ost->mux_timebase);
            else
                pkt.pts += av_rescale_q(sub->end_display_time, AVRational({1, 1000}), ost->mux_timebase);
        }
        pkt.dts = pkt.pts;
        output_packet(of, &pkt, ost, 0);
    }
}

void set_encoder_id(OutputFile *of, OutputStream *ost)
{
    AVDictionaryEntry *e;

    uint8_t *encoder_string;
    int encoder_string_len;
    int format_flags = 0;
    int codec_flags = ost->enc_ctx->flags;

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
    encoder_string = (uint8_t*)av_mallocz(encoder_string_len);
    if (!encoder_string)
    {
        AVException::log(AV_LOG_FATAL,"Failed to allocate a string for encoder string.");
        throw;
    }

    if (!(format_flags & AVFMT_FLAG_BITEXACT) && !(codec_flags & AV_CODEC_FLAG_BITEXACT))
        av_strlcpy((char *)encoder_string, LIBAVCODEC_IDENT " ", encoder_string_len);
    else
        av_strlcpy((char *)encoder_string, "Lavc ", encoder_string_len);
    av_strlcat((char *)encoder_string, ost->enc->name, encoder_string_len);
    av_dict_set(&ost->st->metadata, "encoder", (char *)encoder_string,
                AV_DICT_DONT_STRDUP_VAL | AV_DICT_DONT_OVERWRITE);
}

int decode_interrupt_cb(void *ctx)
{
    return received_nb_signals > atomic_load(&transcode_init_done);
}

void close_all_output_streams(OutputStream *ost, OSTFinished this_stream, OSTFinished others)
{
    int i;
    for (i = 0; i < nb_output_streams; i++)
    {
        OutputStream *ost2 = output_streams[i];
        ost2->finished = (OSTFinished)((int)ost2->finished | (int)((ost == ost2) ? this_stream : others));
    }
}
