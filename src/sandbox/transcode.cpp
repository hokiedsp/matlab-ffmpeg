#include <atomic>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavutil/avassert.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}

#include "transcode_inputfile.h"
#include "transcode_inputstream.h"
#include "transcode_outputfile.h"
#include "transcode_outputstream.h"
#include "transcode_filter.h"
#include "transcode_utils.h"
#include "avexception.h"

// prototypes

std::atomic_int transcode_init_done = 0;
volatile int received_sigterm = 0;
int nb_input_streams = 0;
AVBufferRef *hw_device_ctx;

InputStream **input_streams = NULL;
InputFile **input_files = NULL;
int nb_input_files = 0;
OutputStream **output_streams = NULL;
int nb_output_streams = 0;
OutputFile **output_files = NULL;
int nb_output_files = 0;
FilterGraph **filtergraphs;
int nb_filtergraphs;

// FilterGraph **filtergraphs;
// int        nb_filtergraphs;

int transcode_init(void);
int need_output(void);
int transcode_step(void);
void flush_encoders(void);
int got_eagain(void);
void reset_eagain(void);
int transcode_from_filter(FilterGraph *graph, InputStream **best_ist);
int reap_filters(int flush);

// int filter_nbthreads = 0;

// int check_output_constraints(InputStream *ist, OutputStream *ost);
// void do_streamcopy(InputStream *ist, OutputStream *ost, const AVPacket *pkt);

/*
 * The following code is the main loop of the file converter
 */
int transcode(void)
{
    int ret, i;
    // AVFormatContext *os;
    OutputStream *ost;
    InputStream *ist;
    int64_t timer_start;
    int64_t total_packets_written = 0;

    ret = transcode_init();
    if (ret < 0)
        goto fail;

    // if (stdin_interaction) {
    //     av_log(NULL, AV_LOG_INFO, "Press [q] to stop, [?] for help\n");
    // }

    timer_start = av_gettime_relative();

    // #if HAVE_THREADS
    //     if ((ret = init_input_threads()) < 0)
    //         goto fail;
    // #endif

    while (!received_sigterm)
    {
        int64_t cur_time = av_gettime_relative();

        /* if 'q' pressed, exits */
        // if (stdin_interaction)
        //     if (check_keyboard_interaction(cur_time) < 0)
        //         break;

        /* check if there's any stream where output is still needed */
        if (!need_output())
        {
            AVException::log(AV_LOG_VERBOSE, "No more output streams to write to, finishing.\n");
            break;
        }

        ret = transcode_step();
        if (ret < 0 && ret != AVERROR_EOF)
        {
            AVException::log_error(false, "Error while filtering: %s\n", ret);
            break;
        }

        /* dump report by using the output first video and audio streams */
        // print_report(0, timer_start, cur_time);
    }
    // #if HAVE_THREADS
    // free_input_threads();
    // #endif

    /* at the end of stream, we must flush the decoder buffers */
    for (i = 0; i < nb_input_streams; i++)
    {
        ist = input_streams[i];
        if (!input_files[ist->file_index]->eof_reached)
        {
            process_input_packet(ist, NULL, 0);
        }
    }
    flush_encoders();

    // term_exit();

    /* write the trailer if needed and close file */
    // for (i = 0; i < nb_output_files; i++) {
    //     os = output_files[i]->ctx;
    //     if (!output_files[i]->header_written) {
    //         av_log(NULL, AV_LOG_ERROR,
    //                "Nothing was written into output file %d (%s), because "
    //                "at least one of its streams received no packets.\n",
    //                i, os->url);
    //         continue;
    //     }
    //     if ((ret = av_write_trailer(os)) < 0) {
    //         av_log(NULL, AV_LOG_ERROR, "Error writing trailer of %s: %s\n", os->url, av_err2str(ret));
    //         if (exit_on_error)
    //             exit_program(1);
    //     }
    // }

    /* dump report by using the first video and audio streams */
    // print_report(1, timer_start, av_gettime_relative());

    /* close each encoder */
    for (i = 0; i < nb_output_streams; i++)
    {
        ost = output_streams[i];
        if (ost->encoding_needed)
        {
            av_freep(&ost->enc_ctx->stats_in);
        }
        total_packets_written += ost->packets_written;
    }

    // if (!total_packets_written && (abort_on_flags & ABORT_ON_FLAG_EMPTY_OUTPUT))
    // {
    //     av_log(NULL, AV_LOG_FATAL, "Empty output\n");
    //     // exit_program(1);
    // }

    /* close each decoder */
    for (i = 0; i < nb_input_streams; i++)
    {
        ist = input_streams[i];
        if (ist->decoding_needed)
        {
            avcodec_close(ist->dec_ctx);
            if (ist->hwaccel_uninit)
                ist->hwaccel_uninit(ist->dec_ctx);
        }
    }

    av_buffer_unref(&hw_device_ctx);
    hw_device_free_all();

    /* finished ! */
    ret = 0;

fail:
    // #if HAVE_THREADS
    //     free_input_threads();
    // #endif

    if (output_streams)
    {
        for (i = 0; i < nb_output_streams; i++)
        {
            ost = output_streams[i];
            if (ost)
            {
                // if (ost->logfile)
                // {
                //     if (fclose(ost->logfile))
                //         av_log(NULL, AV_LOG_ERROR,
                //                "Error closing logfile, loss of information possible: %s\n",
                //                av_err2str(AVERROR(errno)));
                //     ost->logfile = NULL;
                // }
                av_freep(&ost->forced_kf_pts);
                av_freep(&ost->apad);
                av_freep(&ost->disposition);
                av_dict_free(&ost->encoder_opts);
                av_dict_free(&ost->sws_dict);
                av_dict_free(&ost->swr_opts);
                av_dict_free(&ost->resample_opts);
            }
        }
    }
    return ret;
}

int transcode_init(void)
{
    int ret = 0, i, j, k;
    AVFormatContext *oc;
    OutputStream *ost;
    // InputStream *ist;
    char error[1024] = {0};

    for (i = 0; i < nb_filtergraphs; i++)
    {
        FilterGraph *fg = filtergraphs[i];
        for (j = 0; j < fg->nb_outputs; j++)
        {
            OutputFilter *ofilter = fg->outputs[j];
            if (!ofilter->ost || ofilter->ost->source_index >= 0)
                continue;
            if (fg->inputs.size() != 1)
                continue;
            for (k = nb_input_streams - 1; k >= 0; k--)
                if (fg->inputs[0]->ist == input_streams[k])
                    break;
            ofilter->ost->source_index = k;
        }
    }

    /* init framerate emulation */
    for (i = 0; i < nb_input_files; i++)
    {
        InputFile *ifile = input_files[i];
        if (ifile->rate_emu)
            for (j = 0; j < ifile->nb_streams; j++)
                input_streams[j + ifile->ist_index]->start = av_gettime_relative();
    }

    /* init input streams */
    for (i = 0; i < nb_input_streams; i++)
        if ((ret = init_input_stream(i, error, sizeof(error))) < 0)
        {
            for (i = 0; i < nb_output_streams; i++)
            {
                ost = output_streams[i];
                avcodec_close(ost->enc_ctx);
            }
            goto dump_format;
        }

    /* open each encoder */
    for (i = 0; i < nb_output_streams; i++)
    {
        // skip streams fed from filtergraphs until we have a frame for them
        if (output_streams[i]->filter)
            continue;

        ret = init_output_stream(output_streams[i], error, sizeof(error));
        if (ret < 0)
            goto dump_format;
    }

    /* discard unused programs */
    for (i = 0; i < nb_input_files; i++)
    {
        InputFile *ifile = input_files[i];
        for (j = 0; j < ifile->ctx->nb_programs; j++)
        {
            AVProgram *p = ifile->ctx->programs[j];
            int discard = AVDISCARD_ALL;

            for (k = 0; k < p->nb_stream_indexes; k++)
                if (!input_streams[ifile->ist_index + p->stream_index[k]]->discard)
                {
                    discard = (int)AVDISCARD_DEFAULT;
                    break;
                }
            p->discard = (AVDiscard)discard;
        }
    }

    /* write headers for files with no streams */
    for (i = 0; i < nb_output_files; i++)
    {
        oc = output_files[i]->ctx;
        if (oc->oformat->flags & AVFMT_NOSTREAMS && oc->nb_streams == 0)
        {
            ret = check_init_output_file(output_files[i], i);
            if (ret < 0)
                goto dump_format;
        }
    }

dump_format:
    /* dump the stream mapping */
    // av_log(NULL, AV_LOG_INFO, "Stream mapping:\n");
    // for (i = 0; i < nb_input_streams; i++) {
    //     ist = input_streams[i];

    //     for (j = 0; j < ist->nb_filters; j++) {
    //         if (!filtergraph_is_simple(ist->filters[j]->graph)) {
    //             av_log(NULL, AV_LOG_INFO, "  Stream #%d:%d (%s) -> %s",
    //                    ist->file_index, ist->st->index, ist->dec ? ist->dec->name : "?",
    //                    ist->filters[j]->name);
    //             if (nb_filtergraphs > 1)
    //                 av_log(NULL, AV_LOG_INFO, " (graph %d)", ist->filters[j]->graph->index);
    //             av_log(NULL, AV_LOG_INFO, "\n");
    //         }
    //     }
    // }

    // for (i = 0; i < nb_output_streams; i++) {
    //     ost = output_streams[i];

    //     if (ost->attachment_filename) {
    //         /* an attached file */
    //         av_log(NULL, AV_LOG_INFO, "  File %s -> Stream #%d:%d\n",
    //                ost->attachment_filename, ost->file_index, ost->index);
    //         continue;
    //     }

    //     if (ost->filter && !filtergraph_is_simple(ost->filter->graph)) {
    //         /* output from a complex graph */
    //         av_log(NULL, AV_LOG_INFO, "  %s", ost->filter->name);
    //         if (nb_filtergraphs > 1)
    //             av_log(NULL, AV_LOG_INFO, " (graph %d)", ost->filter->graph->index);

    //         av_log(NULL, AV_LOG_INFO, " -> Stream #%d:%d (%s)\n", ost->file_index,
    //                ost->index, ost->enc ? ost->enc->name : "?");
    //         continue;
    //     }

    //     av_log(NULL, AV_LOG_INFO, "  Stream #%d:%d -> #%d:%d",
    //            input_streams[ost->source_index]->file_index,
    //            input_streams[ost->source_index]->st->index,
    //            ost->file_index,
    //            ost->index);
    //     if (ost->sync_ist != input_streams[ost->source_index])
    //         av_log(NULL, AV_LOG_INFO, " [sync #%d:%d]",
    //                ost->sync_ist->file_index,
    //                ost->sync_ist->st->index);
    //     if (ost->stream_copy)
    //         av_log(NULL, AV_LOG_INFO, " (copy)");
    //     else {
    //         const AVCodec *in_codec    = input_streams[ost->source_index]->dec;
    //         const AVCodec *out_codec   = ost->enc;
    //         const char *decoder_name   = "?";
    //         const char *in_codec_name  = "?";
    //         const char *encoder_name   = "?";
    //         const char *out_codec_name = "?";
    //         const AVCodecDescriptor *desc;

    //         if (in_codec) {
    //             decoder_name  = in_codec->name;
    //             desc = avcodec_descriptor_get(in_codec->id);
    //             if (desc)
    //                 in_codec_name = desc->name;
    //             if (!strcmp(decoder_name, in_codec_name))
    //                 decoder_name = "native";
    //         }

    //         if (out_codec) {
    //             encoder_name   = out_codec->name;
    //             desc = avcodec_descriptor_get(out_codec->id);
    //             if (desc)
    //                 out_codec_name = desc->name;
    //             if (!strcmp(encoder_name, out_codec_name))
    //                 encoder_name = "native";
    //         }

    //         av_log(NULL, AV_LOG_INFO, " (%s (%s) -> %s (%s))",
    //                in_codec_name, decoder_name,
    //                out_codec_name, encoder_name);
    //     }
    //     av_log(NULL, AV_LOG_INFO, "\n");
    // }

    // if (ret) {
    //     av_log(NULL, AV_LOG_ERROR, "%s\n", error);
    //     return ret;
    // }

    transcode_init_done = 1;

    return 0;
}

/* Return 1 if there remain streams where more output is wanted, 0 otherwise. */
int need_output(void)
{
    int i;

    for (i = 0; i < nb_output_streams; i++)
    {
        OutputStream *ost = output_streams[i];
        OutputFile *of = output_files[ost->file_index];
        AVFormatContext *os = output_files[ost->file_index]->ctx;

        if (ost->finished ||
            (os->pb && avio_tell(os->pb) >= of->limit_filesize))
            continue;
        if (ost->frame_number >= ost->max_frames)
        {
            int j;
            for (j = 0; j < of->ctx->nb_streams; j++)
                close_output_stream(output_streams[of->ost_index + j]);
            continue;
        }

        return 1;
    }

    return 0;
}

/**
 * Run a single step of transcoding.
 *
 * @return  0 for success, <0 for error
 */
int transcode_step(void)
{
    OutputStream *ost;
    InputStream *ist = NULL;
    int ret;

    ost = choose_output();
    if (!ost)
    {
        if (got_eagain())
        {
            reset_eagain();
            av_usleep(10000);
            return 0;
        }
        AVException::log(AV_LOG_VERBOSE, "No more inputs to read from, finishing.\n");
        return AVERROR_EOF;
    }

    if (ost->filter && !ost->filter->graph->graph)
    {
        if (ifilter_has_all_input_formats(ost->filter->graph))
        {
            ret = configure_filtergraph(ost->filter->graph);
            if (ret < 0)
            {
                AVException::log_error(false, "Error reinitializing filters!\n");
                return ret;
            }
        }
    }

    if (ost->filter && ost->filter->graph->graph)
    {
        if (!ost->initialized)
        {
            char error[1024] = {0};
            ret = init_output_stream(ost, error, sizeof(error));
            if (ret < 0)
                AVException::log_error(true,
                                       "Error initializing output stream %d:%d -- %s\n",
                                       ost->file_index, ost->index, error);
        }
        if ((ret = transcode_from_filter(ost->filter->graph, &ist)) < 0)
            return ret;
        if (!ist)
            return 0;
    }
    else if (ost->filter)
    {
        int i;
        for (i = 0; i < ost->filter->graph->inputs.size(); i++)
        {
            InputFilter *ifilter = ost->filter->graph->inputs[i];
            if (!ifilter->ist->got_output && !input_files[ifilter->ist->file_index]->eof_reached)
            {
                ist = ifilter->ist;
                break;
            }
        }
        if (!ist)
        {
            ost->inputs_done = 1;
            return 0;
        }
    }
    else
    {
        av_assert0(ost->source_index >= 0);
        ist = input_streams[ost->source_index];
    }

    ret = process_input(ist->file_index);
    if (ret == AVERROR(EAGAIN))
    {
        if (input_files[ist->file_index]->eagain)
            ost->unavailable = 1;
        return 0;
    }

    if (ret < 0)
        return ret == AVERROR_EOF ? 0 : ret;

    return reap_filters(0);
}

void flush_encoders(void)
{
    int i, ret;

    for (i = 0; i < nb_output_streams; i++)
    {
        OutputStream *ost = output_streams[i];
        AVCodecContext *enc = ost->enc_ctx;
        OutputFile *of = output_files[ost->file_index];

        if (!ost->encoding_needed)
            continue;

        // Try to enable encoding with no input frames.
        // Maybe we should just let encoding fail instead.
        if (!ost->initialized)
        {
            FilterGraph *fg = ost->filter->graph;
            char error[1024] = "";

            AVException::log(AV_LOG_WARNING,
                             "Finishing stream %d:%d without any data written to it.\n",
                             ost->file_index, ost->st->index);

            if (ost->filter && !fg->graph)
            {
                int x;
                for (x = 0; x < fg->inputs.size(); x++)
                {
                    InputFilter *ifilter = fg->inputs[x];
                    if (ifilter->format < 0)
                        ifilter_parameters_from_codecpar(ifilter, ifilter->ist->st->codecpar);
                }

                if (!ifilter_has_all_input_formats(fg))
                    continue;

                ret = configure_filtergraph(fg);
                if (ret < 0)
                    AVException::log_error(true, "Error configuring filter graph\n");

                finish_output_stream(ost);
            }

            ret = init_output_stream(ost, error, sizeof(error));
            if (ret < 0)
                AVException::log_error(true,
                                       "Error initializing output stream %d:%d -- %s\n",
                                       2, ost->file_index, ost->index, error);
        }

        if (enc->codec_type == AVMEDIA_TYPE_AUDIO && enc->frame_size <= 1)
            continue;

        if (enc->codec_type != AVMEDIA_TYPE_VIDEO && enc->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;

        for (;;)
        {
            const char *desc = NULL;
            AVPacket pkt;
            int pkt_size;

            switch (enc->codec_type)
            {
            case AVMEDIA_TYPE_AUDIO:
                desc = "audio";
                break;
            case AVMEDIA_TYPE_VIDEO:
                desc = "video";
                break;
            default:
                av_assert0(0); // triggers PANIC log & aborts
            }

            av_init_packet(&pkt);
            pkt.data = NULL;
            pkt.size = 0;

            // update_benchmark(NULL);

            while ((ret = avcodec_receive_packet(enc, &pkt)) == AVERROR(EAGAIN))
            {
                ret = avcodec_send_frame(enc, NULL);
                if (ret < 0)
                    AVException::log_error(true, "%s encoding failed: %s\n", 1, desc, ret);
            }

            // update_benchmark("flush_%s %d.%d", desc, ost->file_index, ost->index);
            if (ret < 0 && ret != AVERROR_EOF)
                AVException::log_error(true, "%s encoding failed: %s\n", 1, desc, ret);

            // if (ost->logfile && enc->stats_out)
            // {
            //     fprintf(ost->logfile, "%s", enc->stats_out);
            // }
            if (ret == AVERROR_EOF)
            {
                output_packet(of, &pkt, ost, 1);
                break;
            }
            if (ost->finished & MUXER_FINISHED)
            {
                av_packet_unref(&pkt);
                continue;
            }
            av_packet_rescale_ts(&pkt, enc->time_base, ost->mux_timebase);
            pkt_size = pkt.size;
            output_packet(of, &pkt, ost, 0);
            // if (ost->enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO && vstats_filename) {
            //     do_video_stats(ost, pkt_size);
        }
    }
}

int got_eagain(void)
{
    int i;
    for (i = 0; i < nb_output_streams; i++)
        if (output_streams[i]->unavailable)
            return 1;
    return 0;
}

void reset_eagain(void)
{
    int i;
    for (i = 0; i < nb_input_files; i++)
        input_files[i]->eagain = 0;
    for (i = 0; i < nb_output_streams; i++)
        output_streams[i]->unavailable = 0;
}

/**
 * Perform a step of transcoding for the specified filter graph.
 *
 * @param[in]  graph     filter graph to consider
 * @param[out] best_ist  input stream where a frame would allow to continue
 * @return  0 for success, <0 for error
 */
int transcode_from_filter(FilterGraph *graph, InputStream **best_ist)
{
    int i, ret;
    int nb_requests, nb_requests_max = 0;
    InputFilter *ifilter;
    InputStream *ist;

    *best_ist = NULL;
    ret = avfilter_graph_request_oldest(graph->graph);
    if (ret >= 0)
        return reap_filters(0);

    if (ret == AVERROR_EOF)
    {
        ret = reap_filters(1);
        for (i = 0; i < graph->nb_outputs; i++)
            close_output_stream(graph->outputs[i]->ost);
        return ret;
    }
    if (ret != AVERROR(EAGAIN))
        return ret;

    for (i = 0; i < graph->inputs.size(); i++)
    {
        ifilter = graph->inputs[i];
        ist = ifilter->ist;
        if (input_files[ist->file_index]->eagain ||
            input_files[ist->file_index]->eof_reached)
            continue;
        nb_requests = av_buffersrc_get_nb_failed_requests(ifilter->filter);
        if (nb_requests > nb_requests_max)
        {
            nb_requests_max = nb_requests;
            *best_ist = ist;
        }
    }

    if (!*best_ist)
        for (i = 0; i < graph->nb_outputs; i++)
            graph->outputs[i]->ost->unavailable = 1;

    return 0;
}

/*
 * Check whether a packet from ist should be written into ost at this time
 */
// int check_output_constraints(InputStream *ist, OutputStream *ost)
// {
//     OutputFile *of = output_files[ost->file_index];
//     int ist_index  = input_files[ist->file_index]->ist_index + ist->st->index;

//     if (ost->source_index != ist_index)
//         return 0;

//     if (ost->finished)
//         return 0;

//     if (of->start_time != AV_NOPTS_VALUE && ist->pts < of->start_time)
//         return 0;

//     return 1;
// }

// void do_streamcopy(InputStream *ist, OutputStream *ost, const AVPacket *pkt)
// {
//     OutputFile *of = output_files[ost->file_index];
//     InputFile   *f = input_files [ist->file_index];
//     int64_t start_time = (of->start_time == AV_NOPTS_VALUE) ? 0 : of->start_time;
//     int64_t ost_tb_start_time = av_rescale_q(start_time, AV_TIME_BASE_Q, ost->mux_timebase);
//     AVPacket opkt = { 0 };

//     av_init_packet(&opkt);

//     // EOF: flush output bitstream filters.
//     if (!pkt) {
//         output_packet(of, &opkt, ost, 1);
//         return;
//     }

//     if ((!ost->frame_number && !(pkt->flags & AV_PKT_FLAG_KEY)) &&
//         !ost->copy_initial_nonkeyframes)
//         return;

//     if (!ost->frame_number && !ost->copy_prior_start) {
//         int64_t comp_start = start_time;
//         if (copy_ts && f->start_time != AV_NOPTS_VALUE)
//             comp_start = FFMAX(start_time, f->start_time + f->ts_offset);
//         if (pkt->pts == AV_NOPTS_VALUE ?
//             ist->pts < comp_start :
//             pkt->pts < av_rescale_q(comp_start, AV_TIME_BASE_Q, ist->st->time_base))
//             return;
//     }

//     if (of->recording_time != INT64_MAX &&
//         ist->pts >= of->recording_time + start_time) {
//         close_output_stream(ost);
//         return;
//     }

//     if (f->recording_time != INT64_MAX) {
//         start_time = f->ctx->start_time;
//         if (f->start_time != AV_NOPTS_VALUE && copy_ts)
//             start_time += f->start_time;
//         if (ist->pts >= f->recording_time + start_time) {
//             close_output_stream(ost);
//             return;
//         }
//     }

//     /* force the input stream PTS */
//     if (ost->enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
//         ost->sync_opts++;

//     if (pkt->pts != AV_NOPTS_VALUE)
//         opkt.pts = av_rescale_q(pkt->pts, ist->st->time_base, ost->mux_timebase) - ost_tb_start_time;
//     else
//         opkt.pts = AV_NOPTS_VALUE;

//     if (pkt->dts == AV_NOPTS_VALUE)
//         opkt.dts = av_rescale_q(ist->dts, AV_TIME_BASE_Q, ost->mux_timebase);
//     else
//         opkt.dts = av_rescale_q(pkt->dts, ist->st->time_base, ost->mux_timebase);
//     opkt.dts -= ost_tb_start_time;

//     if (ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && pkt->dts != AV_NOPTS_VALUE) {
//         int duration = av_get_audio_frame_duration(ist->dec_ctx, pkt->size);
//         if(!duration)
//             duration = ist->dec_ctx->frame_size;
//         opkt.dts = opkt.pts = av_rescale_delta(ist->st->time_base, pkt->dts,
//                                                (AVRational){1, ist->dec_ctx->sample_rate}, duration, &ist->filter_in_rescale_delta_last,
//                                                ost->mux_timebase) - ost_tb_start_time;
//     }

//     opkt.duration = av_rescale_q(pkt->duration, ist->st->time_base, ost->mux_timebase);

//     opkt.flags    = pkt->flags;

//     if (pkt->buf) {
//         opkt.buf = av_buffer_ref(pkt->buf);
//         if (!opkt.buf)
//             exit_program(1);
//     }
//     opkt.data = pkt->data;
//     opkt.size = pkt->size;

//     av_copy_packet_side_data(&opkt, pkt);

//     output_packet(of, &opkt, ost, 0);
// }
