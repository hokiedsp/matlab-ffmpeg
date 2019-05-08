
#include "transcode_inputfile.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
// #include <libavutil/opt.h>
#include <libavutil/timestamp.h>
}

#include "avexception.h"
#include "transcode_outputstream.h"
#include "transcode_utils.h"
#include "transcode_hw.h"

InputStream **input_streams = NULL;
InputFile **input_files = NULL;
OutputStream **output_streams = NULL;
int exit_on_error = 0;
int nb_output_streams = 0;
int copy_ts = 0;
float dts_delta_threshold = 10;
float dts_error_threshold   = 3600*30;
void reset_eagain(void);

// input file
int get_input_packet(InputFile *f, AVPacket *pkt);
int seek_to_start(InputFile *ifile, AVFormatContext *is);
void report_new_stream(int input_index, AVPacket *pkt);

/*
 * Return
 * - 0 -- one packet was read and processed
 * - AVERROR(EAGAIN) -- no packets were available for selected file,
 *   this function should be called again
 * - AVERROR_EOF -- this function should not be called again
 */
int process_input(int file_index)
{
    InputFile *ifile = input_files[file_index];
    AVFormatContext *is;
    InputStream *ist;
    AVPacket pkt;
    int ret, i, j;
    // int thread_ret;
    int64_t duration;
    int64_t pkt_dts;

    is = ifile->ctx;
    ret = get_input_packet(ifile, &pkt);

    if (ret == AVERROR(EAGAIN))
    {
        ifile->eagain = 1;
        return ret;
    }
    if (ret < 0 && ifile->loop)
    {
        AVCodecContext *avctx;
        for (i = 0; i < ifile->nb_streams; i++)
        {
            ist = input_streams[ifile->ist_index + i];
            avctx = ist->dec_ctx;
            if (ist->decoding_needed)
            {
                ret = process_input_packet(ist, NULL, 1);
                if (ret > 0)
                    return 0;
                avcodec_flush_buffers(avctx);
            }
        }
        // #if HAVE_THREADS
        //         free_input_thread(file_index);
        // #endif
        ret = seek_to_start(ifile, is);
        // #if HAVE_THREADS
        //         thread_ret = init_input_thread(file_index);
        //         if (thread_ret < 0)
        //             return thread_ret;
        // #endif
        if (ret < 0)
            AVException::log(AV_LOG_WARNING, "Seek to start failed.\n");
        else
            ret = get_input_packet(ifile, &pkt);
        if (ret == AVERROR(EAGAIN))
        {
            ifile->eagain = 1;
            return ret;
        }
    }
    if (ret < 0)
    {
        if (ret != AVERROR_EOF)
        {
            // print_error(is->url, ret);
            if (exit_on_error)
                AVException::log_error(AV_LOG_FATAL, "Failed to get next input packet: %s", ret);
        }

        for (i = 0; i < ifile->nb_streams; i++)
        {
            ist = input_streams[ifile->ist_index + i];
            if (ist->decoding_needed)
            {
                ret = process_input_packet(ist, NULL, 0);
                if (ret > 0)
                    return 0;
            }

            /* mark all outputs that don't go through lavfi as finished */
            for (j = 0; j < nb_output_streams; j++)
            {
                OutputStream *ost = output_streams[j];

                if (ost->source_index == ifile->ist_index + i &&
                    ost->enc->type == AVMEDIA_TYPE_SUBTITLE)
                    // (ost->stream_copy || ost->enc->type == AVMEDIA_TYPE_SUBTITLE))
                    finish_output_stream(ost);
            }
        }

        ifile->eof_reached = 1;
        return AVERROR(EAGAIN);
    }

    reset_eagain();

    // if (do_pkt_dump)
    // {
    //     av_pkt_dump_log2(NULL, AV_LOG_INFO, &pkt, do_hex_dump,
    //                      is->streams[pkt.stream_index]);
    // }
    /* the following test is needed in case new streams appear
       dynamically in stream : we ignore them */
    if (pkt.stream_index >= ifile->nb_streams)
    {
        report_new_stream(file_index, &pkt);
        goto discard_packet;
    }

    ist = input_streams[ifile->ist_index + pkt.stream_index];

    ist->data_size += pkt.size;
    ist->nb_packets++;

    if (ist->discard)
        goto discard_packet;

    if (pkt.flags & AV_PKT_FLAG_CORRUPT)
        AVException::log(exit_on_error ? AV_LOG_FATAL : AV_LOG_WARNING,
                         "%s: corrupt input packet in stream %d\n",
                         is->url, pkt.stream_index);

    // if (debug_ts)
    // {
    //     AVException::(AV_LOG_INFO, "demuxer -> ist_index:%d type:%s "
    //                                "next_dts:%s next_dts_time:%s next_pts:%s next_pts_time:%s pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s off:%s off_time:%s\n",
    //                   ifile->ist_index + pkt.stream_index, av_get_media_type_string(ist->dec_ctx->codec_type),
    //                   av_ts2str(ist->next_dts), av_ts2timestr(ist->next_dts, &AV_TIME_BASE_Q),
    //                   av_ts2str(ist->next_pts), av_ts2timestr(ist->next_pts, &AV_TIME_BASE_Q),
    //                   av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &ist->st->time_base),
    //                   av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &ist->st->time_base),
    //                   av_ts2str(input_files[ist->file_index]->ts_offset),
    //                   av_ts2timestr(input_files[ist->file_index]->ts_offset, &AV_TIME_BASE_Q));
    // }

    if (!ist->wrap_correction_done && is->start_time != AV_NOPTS_VALUE && ist->st->pts_wrap_bits < 64)
    {
        int64_t stime, stime2;
        // Correcting starttime based on the enabled streams
        // FIXME this ideally should be done before the first use of starttime but we do not know which are the enabled streams at that point.
        //       so we instead do it here as part of discontinuity handling
        if (ist->next_dts == AV_NOPTS_VALUE && ifile->ts_offset == -is->start_time && (is->iformat->flags & AVFMT_TS_DISCONT))
        {
            int64_t new_start_time = INT64_MAX;
            for (i = 0; i < is->nb_streams; i++)
            {
                AVStream *st = is->streams[i];
                if (st->discard == AVDISCARD_ALL || st->start_time == AV_NOPTS_VALUE)
                    continue;
                new_start_time = FFMIN(new_start_time, av_rescale_q(st->start_time, st->time_base, AV_TIME_BASE_Q));
            }
            if (new_start_time > is->start_time)
            {
                av_log(is, AV_LOG_VERBOSE, "Correcting start time by %" PRId64 "\n", new_start_time - is->start_time);
                ifile->ts_offset = -new_start_time;
            }
        }

        stime = av_rescale_q(is->start_time, AV_TIME_BASE_Q, ist->st->time_base);
        stime2 = stime + (1ULL << ist->st->pts_wrap_bits);
        ist->wrap_correction_done = 1;

        if (stime2 > stime && pkt.dts != AV_NOPTS_VALUE && pkt.dts > stime + (1LL << (ist->st->pts_wrap_bits - 1)))
        {
            pkt.dts -= 1ULL << ist->st->pts_wrap_bits;
            ist->wrap_correction_done = 0;
        }
        if (stime2 > stime && pkt.pts != AV_NOPTS_VALUE && pkt.pts > stime + (1LL << (ist->st->pts_wrap_bits - 1)))
        {
            pkt.pts -= 1ULL << ist->st->pts_wrap_bits;
            ist->wrap_correction_done = 0;
        }
    }

    /* add the stream-global side data to the first packet */
    if (ist->nb_packets == 1)
    {
        for (i = 0; i < ist->st->nb_side_data; i++)
        {
            AVPacketSideData *src_sd = &ist->st->side_data[i];
            uint8_t *dst_data;

            if (src_sd->type == AV_PKT_DATA_DISPLAYMATRIX)
                continue;

            if (av_packet_get_side_data(&pkt, src_sd->type, NULL))
                continue;

            dst_data = av_packet_new_side_data(&pkt, src_sd->type, src_sd->size);
            if (!dst_data)
                AVException::log(AV_LOG_FATAL, "Failed to allocate new information of a packet.");

            memcpy(dst_data, src_sd->data, src_sd->size);
        }
    }

    if (pkt.dts != AV_NOPTS_VALUE)
        pkt.dts += av_rescale_q(ifile->ts_offset, AV_TIME_BASE_Q, ist->st->time_base);
    if (pkt.pts != AV_NOPTS_VALUE)
        pkt.pts += av_rescale_q(ifile->ts_offset, AV_TIME_BASE_Q, ist->st->time_base);

    if (pkt.pts != AV_NOPTS_VALUE)
        pkt.pts *= ist->ts_scale;
    if (pkt.dts != AV_NOPTS_VALUE)
        pkt.dts *= ist->ts_scale;

    pkt_dts = av_rescale_q_rnd(pkt.dts, ist->st->time_base, AV_TIME_BASE_Q, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    if ((ist->dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ||
         ist->dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) &&
        pkt_dts != AV_NOPTS_VALUE && ist->next_dts == AV_NOPTS_VALUE && !copy_ts && (is->iformat->flags & AVFMT_TS_DISCONT) && ifile->last_ts != AV_NOPTS_VALUE)
    {
        int64_t delta = pkt_dts - ifile->last_ts;
        if (delta < -1LL * dts_delta_threshold * AV_TIME_BASE ||
            delta > 1LL * dts_delta_threshold * AV_TIME_BASE)
        {
            ifile->ts_offset -= delta;
            AVException::log(AV_LOG_DEBUG,
                             "Inter stream timestamp discontinuity %" PRId64 ", new offset= %" PRId64 "\n",
                             delta, ifile->ts_offset);
            pkt.dts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
            if (pkt.pts != AV_NOPTS_VALUE)
                pkt.pts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
        }
    }

    duration = av_rescale_q(ifile->duration, ifile->time_base, ist->st->time_base);
    if (pkt.pts != AV_NOPTS_VALUE)
    {
        pkt.pts += duration;
        ist->max_pts = FFMAX(pkt.pts, ist->max_pts);
        ist->min_pts = FFMIN(pkt.pts, ist->min_pts);
    }

    if (pkt.dts != AV_NOPTS_VALUE)
        pkt.dts += duration;

    pkt_dts = av_rescale_q_rnd(pkt.dts, ist->st->time_base, AV_TIME_BASE_Q, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    if ((ist->dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ||
         ist->dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) &&
        pkt_dts != AV_NOPTS_VALUE && ist->next_dts != AV_NOPTS_VALUE &&
        !copy_ts)
    {
        int64_t delta = pkt_dts - ist->next_dts;
        if (is->iformat->flags & AVFMT_TS_DISCONT)
        {
            if (delta < -1LL * dts_delta_threshold * AV_TIME_BASE ||
                delta > 1LL * dts_delta_threshold * AV_TIME_BASE ||
                pkt_dts + AV_TIME_BASE / 10 < FFMAX(ist->pts, ist->dts))
            {
                ifile->ts_offset -= delta;
                AVException::log(AV_LOG_DEBUG,
                                 "timestamp discontinuity %" PRId64 ", new offset= %" PRId64 "\n",
                                 delta, ifile->ts_offset);
                pkt.dts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
                if (pkt.pts != AV_NOPTS_VALUE)
                    pkt.pts -= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
            }
        }
        else
        {
            if (delta < -1LL * dts_error_threshold * AV_TIME_BASE ||
                delta > 1LL * dts_error_threshold * AV_TIME_BASE)
            {
                AVException::log(AV_LOG_WARNING, "DTS %" PRId64 ", next:%" PRId64 " st:%d invalid dropping\n", pkt.dts, ist->next_dts, pkt.stream_index);
                pkt.dts = AV_NOPTS_VALUE;
            }
            if (pkt.pts != AV_NOPTS_VALUE)
            {
                int64_t pkt_pts = av_rescale_q(pkt.pts, ist->st->time_base, AV_TIME_BASE_Q);
                delta = pkt_pts - ist->next_dts;
                if (delta < -1LL * dts_error_threshold * AV_TIME_BASE ||
                    delta > 1LL * dts_error_threshold * AV_TIME_BASE)
                {
                    AVException::log(AV_LOG_WARNING, "PTS %" PRId64 ", next:%" PRId64 " invalid dropping st:%d\n", pkt.pts, ist->next_dts, pkt.stream_index);
                    pkt.pts = AV_NOPTS_VALUE;
                }
            }
        }
    }

    if (pkt.dts != AV_NOPTS_VALUE)
        ifile->last_ts = av_rescale_q(pkt.dts, ist->st->time_base, AV_TIME_BASE_Q);

    // if (debug_ts)
    // {
    //     AVException::log(AV_LOG_INFO, "demuxer+ffmpeg -> ist_index:%d type:%s pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s off:%s off_time:%s\n",
    //                      ifile->ist_index + pkt.stream_index, av_get_media_type_string(ist->dec_ctx->codec_type),
    //                      av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &ist->st->time_base),
    //                      av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &ist->st->time_base),
    //                      av_ts2str(input_files[ist->file_index]->ts_offset),
    //                      av_ts2timestr(input_files[ist->file_index]->ts_offset, &AV_TIME_BASE_Q));
    // }

    sub2video_heartbeat(ist, pkt.pts);

    process_input_packet(ist, &pkt, 0);

discard_packet:
    av_packet_unref(&pkt);

    return 0;
}

int get_input_packet(InputFile *f, AVPacket *pkt)
{
    if (f->rate_emu)
    {
        int i;
        for (i = 0; i < f->nb_streams; i++)
        {
            InputStream *ist = input_streams[f->ist_index + i];
            int64_t pts = av_rescale(ist->dts, 1000000, AV_TIME_BASE);
            int64_t now = av_gettime_relative() - ist->start;
            if (pts > now)
                return AVERROR(EAGAIN);
        }
    }

    // #if HAVE_THREADS
    //     if (nb_input_files > 1)
    //         return get_input_packet_mt(f, pkt);
    // #endif
    return av_read_frame(f->ctx, pkt);
}

int seek_to_start(InputFile *ifile, AVFormatContext *is)
{
    InputStream *ist;
    AVCodecContext *avctx;
    int i, ret, has_audio = 0;
    int64_t duration = 0;

    ret = av_seek_frame(is, -1, is->start_time, 0);
    if (ret < 0)
        return ret;

    for (i = 0; i < ifile->nb_streams; i++)
    {
        ist = input_streams[ifile->ist_index + i];
        avctx = ist->dec_ctx;

        /* duration is the length of the last frame in a stream
         * when audio stream is present we don't care about
         * last video frame length because it's not defined exactly */
        if (avctx->codec_type == AVMEDIA_TYPE_AUDIO && ist->nb_samples)
            has_audio = 1;
    }

    for (i = 0; i < ifile->nb_streams; i++)
    {
        ist = input_streams[ifile->ist_index + i];
        avctx = ist->dec_ctx;

        if (has_audio)
        {
            if (avctx->codec_type == AVMEDIA_TYPE_AUDIO && ist->nb_samples)
            {
                AVRational sample_rate = {1, avctx->sample_rate};

                duration = av_rescale_q(ist->nb_samples, sample_rate, ist->st->time_base);
            }
            else
            {
                continue;
            }
        }
        else
        {
            if (ist->framerate.num)
            {
                duration = av_rescale_q(1, av_inv_q(ist->framerate), ist->st->time_base);
            }
            else if (ist->st->avg_frame_rate.num)
            {
                duration = av_rescale_q(1, av_inv_q(ist->st->avg_frame_rate), ist->st->time_base);
            }
            else
            {
                duration = 1;
            }
        }
        if (!ifile->duration)
            ifile->time_base = ist->st->time_base;
        /* the total duration of the stream, max_pts - min_pts is
         * the duration of the stream without the last frame */
        duration += ist->max_pts - ist->min_pts;
        ifile->time_base = duration_max(duration, &ifile->duration, ist->st->time_base,
                                        ifile->time_base);
    }

    if (ifile->loop > 0)
        ifile->loop--;

    return ret;
}

void report_new_stream(int input_index, AVPacket *pkt)
{
    InputFile *file = input_files[input_index];
    AVStream *st = file->ctx->streams[pkt->stream_index];

    if (pkt->stream_index < file->nb_streams_warn)
        return;
    av_log(file->ctx, AV_LOG_WARNING,
           "New %s stream %d:%d at pos:%" PRId64 " and DTS:%ss\n",
           av_get_media_type_string(st->codecpar->codec_type),
           input_index, pkt->stream_index,
           pkt->pos, av_ts2timestr(pkt->dts, &st->time_base));
    file->nb_streams_warn = pkt->stream_index + 1;
}
