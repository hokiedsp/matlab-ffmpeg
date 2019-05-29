#include "transcode_inputstream.h"

#include <sstream> // std::stringstream

extern "C"
{
#include <libavutil/parseutils.h>
#include <libavutil/avassert.h>
// #include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
    // #include <libavformat/avformat.h>
}

#include "../ffmpeg/avexception.h"
#include "transcode_inputfile.h"

#include "transcode_utils.h"
#include "transcode_outputstream.h"
#include "transcode_outputfile.h"
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
    {"videotoolbox", videotoolbox_init, HWACCEL_VIDEOTOOLBOX, AV_PIX_FMT_VIDEOTOOLBOX},
#endif
#if CONFIG_LIBMFX
    {"qsv", qsv_init, HWACCEL_QSV, AV_PIX_FMT_QSV},
#endif
#if CONFIG_CUVID
    {"cuvid", cuvid_init, HWACCEL_CUVID, AV_PIX_FMT_CUDA},
#endif
    {0},
};

// prototypes
void sub2video_copy_rect(uint8_t *dst, int dst_linesize, int w, int h, AVSubtitleRect *r);

bool InputStream::bitexact = false;

// int init_input_stream(int ist_index, char *error, int error_len)
InputStream::InputStream(InputFile &infile, int index, bool open_codec, const AVDictionary *codec_opts)
    : file(infile), st(infile.ctx->streams[index]), dec_ctx(nullptr), dec(nullptr), decoded_frame(nullptr), filter_frame(nullptr),
      start(0), next_dts(0), dts(0), next_pts(0), pts(0), wrap_correction_done(false), filter_in_rescale_delta_last(0), min_pts(0),
      max_pts(0), cfr_next_pts(0), nb_samples(0), ts_scale(0.0), saw_first_ts(false), decoder_opts(nullptr), framerate({1, 1}),
      top_field_first(false), guess_layout_max(INTMAX_MAX), autorotate(false), fix_sub_duration(false),
      prev_sub({false, 0, {0, 0, 0, 0, nullptr, 0}}), sub2video({0, 0, nullptr, nullptr, 0, 0}), reinit_filters(false),
      hwaccel_id(HWACCEL_AUTO), hwaccel_device_type(AV_HWDEVICE_TYPE_NONE), hwaccel_device(nullptr), hwaccel_output_format(AV_PIX_FMT_NONE),
      hwaccel_uninit(nullptr), hwaccel_get_buffer(nullptr), hwaccel_retrieve_data(nullptr), hwaccel_pix_fmt(AV_PIX_FMT_NONE),
      hwaccel_retrieved_pix_fmt(AV_PIX_FMT_NONE), hw_frames_ctx(nullptr), data_size(0), nb_packets(0), frames_decoded(0), samples_decoded(0),
      dts_buffer(nullptr), nb_dts_buffer(0), got_output(false)
{
    discard = true;
    st->discard = AVDISCARD_ALL;

    nb_samples = 0;
    min_pts = INT64_MAX;
    max_pts = INT64_MIN;
    ts_scale = 1.0;
    autorotate = 1;
    reinit_filters = -1;
    // user_set_discard = AVDISCARD_NONE;
    filter_in_rescale_delta_last = AV_NOPTS_VALUE;
    top_field_first = -1;
    guess_layout_max = INT_MAX;

    if (open_codec)
        initDecoder(codec_opts);
}

std::string InputStream::IdString() const
{
    std::stringstream ss;
    ss << '#' << file.index << ':' << st->index;
    return ss.str();
}

void InputStream::setDecoder(const std::string &name_str)
{
    if (dec_ctx)
    {
        AVException::log(AV_LOG_ERROR, "Decoder cannot be set once open.");
        throw;
    }

    const char *name = name_str.c_str();
    enum AVMediaType type = st->codecpar->codec_type;
    const AVCodecDescriptor *desc;

    AVCodec *codec = avcodec_find_decoder_by_name(name);

    if (!codec && (desc = avcodec_descriptor_get_by_name(name)))
    {
        codec = avcodec_find_decoder(desc->id);
        if (codec)
            AVException::log(AV_LOG_VERBOSE, "Matched decoder '%s' for codec '%s'.", codec->name, desc->name);
    }

    if (!codec)
    {
        AVException::log(AV_LOG_ERROR, "Unknown decoder '%s'", name);
        throw;
    }
    if (codec->type != st->codecpar->codec_type)
    {
        AVException::log(AV_LOG_ERROR, "Invalid decoder type '%s'", name);
        throw;
    }

    st->codecpar->codec_id = codec->id;
}

void InputStream::setHWAccel(const std::string &hwaccel, const std::string &hwaccel_device, const std::string &hwaccel_output_format)
{
    // to-do
    //

    // if (hwaccel)
    // {
    //     // The NVDEC hwaccels use a CUDA device, so remap the name here.
    //     if (!strcmp(hwaccel, "nvdec"))
    //         hwaccel = "cuda";

    //     if (!strcmp(hwaccel, "none"))
    //         hwaccel_id = HWACCEL_NONE;
    //     else if (!strcmp(hwaccel, "auto"))
    //         hwaccel_id = HWACCEL_AUTO;
    //     else
    //     {
    //         enum AVHWDeviceType type;
    //         int i;
    //         for (i = 0; hwaccels[i].name; i++)
    //         {
    //             if (!strcmp(hwaccels[i].name, hwaccel))
    //             {
    //                 hwaccel_id = hwaccels[i].id;
    //                 break;
    //             }
    //         }

    //         if (!hwaccel_id)
    //         {
    //             type = av_hwdevice_find_type_by_name(hwaccel);
    //             if (type != AV_HWDEVICE_TYPE_NONE)
    //             {
    //                 hwaccel_id = HWACCEL_GENERIC;
    //                 hwaccel_device_type = type;
    //             }
    //         }

    //         if (!hwaccel_id)
    //         {
    //             av_log(NULL, AV_LOG_FATAL, "Unrecognized hwaccel: %s.",
    //                    hwaccel);
    //             av_log(NULL, AV_LOG_FATAL, "Supported hwaccels: ");
    //             type = AV_HWDEVICE_TYPE_NONE;
    //             while ((type = av_hwdevice_iterate_types(type)) !=
    //                    AV_HWDEVICE_TYPE_NONE)
    //                 av_log(NULL, AV_LOG_FATAL, "%s ",
    //                        av_hwdevice_get_type_name(type));
    //             for (i = 0; hwaccels[i].name; i++)
    //                 av_log(NULL, AV_LOG_FATAL, "%s ", hwaccels[i].name);
    //             av_log(NULL, AV_LOG_FATAL, "");
    //             exit_program(1);
    //         }
    //     }
    // }

    // if (hwaccel_device)
    // {
    //     hwaccel_device = av_strdup(hwaccel_device);
    //     if (!hwaccel_device)
    //         exit_program(1);
    // }

    // if (hwaccel_output_format)
    // {
    //     hwaccel_output_format = av_get_pix_fmt(hwaccel_output_format);
    //     if (hwaccel_output_format == AV_PIX_FMT_NONE)
    //     {
    //         av_log(NULL, AV_LOG_FATAL, "Unrecognised hwaccel output "
    //                                    "format: %s",
    //                hwaccel_output_format);
    //     }
    // }
    // else
    // {
    //     hwaccel_output_format = AV_PIX_FMT_NONE;
    // }

    // hwaccel_pix_fmt = AV_PIX_FMT_NONE;
}

void InputStream::initDecoder(const AVDictionary *codec_opts)
{
    if (dec_ctx)
    {
        AVException::log(AV_LOG_ERROR, "Error allocating the decoder context.");
        throw; // just in case
    }

    AVCodecParameters *par = st->codecpar;
    // char *framerate = NULL, *hwaccel_device = NULL;
    // const char *hwaccel = NULL;
    // char *hwaccel_output_format = NULL;
    // char *codec_tag = NULL;
    // char *next;
    // char *discard_str = NULL;
    // const AVClass *cc = avcodec_get_class();
    // const AVOption *discard_opt = av_opt_find(&cc, "skip_frame", NULL, 0, 0);

    // get codec
    dec = avcodec_find_decoder(par->codec_id);

    // set options
    if (codec_opts)
    {
        // o->g->codec_opts, ist->st->codecpar->codec_id, ic, st, ist->dec
        // (AVDictionary *opts, enum AVCodecID codec_id,
        //                         AVFormatContext *s, AVStream *st, AVCodec *codec)
        decoder_opts = NULL;
        AVDictionaryEntry *t = NULL;
        int flags = AV_OPT_FLAG_DECODING_PARAM;
        char prefix = 0;
        const AVClass *cc = avcodec_get_class();

        switch (st->codecpar->codec_type)
        {
        case AVMEDIA_TYPE_VIDEO:
            prefix = 'v';
            flags |= AV_OPT_FLAG_VIDEO_PARAM;
            break;
        case AVMEDIA_TYPE_AUDIO:
            prefix = 'a';
            flags |= AV_OPT_FLAG_AUDIO_PARAM;
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            prefix = 's';
            flags |= AV_OPT_FLAG_SUBTITLE_PARAM;
            break;
        }

        while (t = av_dict_get(codec_opts, "", t, AV_DICT_IGNORE_SUFFIX))
        {
            char *p = strchr(t->key, ':');

            /* check stream specification in opt name */
            if (p)
            {
                switch (avformat_match_stream_specifier(file.ctx, st, p + 1))
                {
                case 1: // matched
                    *p = 0;
                    break;
                case 0: // not matched
                    continue;
                default:
                    AVException::log(file.ctx, AV_LOG_ERROR, "Invalid stream specifier: %s.", p + 1);
                    throw;
                }
            }
            if (av_opt_find(&cc, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) || !dec ||
                (dec->priv_class && av_opt_find(&dec->priv_class, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ)))
                av_dict_set(&decoder_opts, t->key, t->value, 0);
            else if (t->key[0] == prefix && av_opt_find(&cc, t->key + 1, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ))
                av_dict_set(&decoder_opts, t->key + 1, t->value, 0);

            if (p)
                *p = ':';
        }
    }

    dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx)
    {
        AVException::log(AV_LOG_FATAL, "Error allocating the decoder context.");
        throw; // just in case
    }

    if (avcodec_parameters_to_context(dec_ctx, st->codecpar) < 0)
    {
        AVException::log(AV_LOG_FATAL, "Error initializing the decoder context.");
        throw; // just in case
    }

    if (bitexact)
        dec_ctx->flags |= AV_CODEC_FLAG_BITEXACT;

    switch (par->codec_type)
    {
    case AVMEDIA_TYPE_VIDEO:
        dec_ctx->framerate = st->avg_frame_rate;
        break;
    case AVMEDIA_TYPE_AUDIO:
        if (!dec_ctx->channel_layout && dec_ctx->channels <= guess_layout_max)
        {
            dec_ctx->channel_layout = av_get_default_channel_layout(dec_ctx->channels);
            if (dec_ctx->channel_layout)
            {
                char layout_name[256];
                av_get_channel_layout_string(layout_name, sizeof(layout_name), dec_ctx->channels, dec_ctx->channel_layout);

                AVException::log(AV_LOG_WARNING, "Guessed Channel Layout for Input Stream %s : %s",
                                 IdString().c_str(), layout_name);
            }
        }
        break;
    case AVMEDIA_TYPE_DATA:
    case AVMEDIA_TYPE_SUBTITLE:
    {
        char *canvas_size = NULL;
        if (canvas_size &&
            av_parse_video_size(&dec_ctx->width, &dec_ctx->height, canvas_size) < 0)
        {
            AVException::log(AV_LOG_FATAL, "Invalid canvas size: %s.", canvas_size);
            throw;
        }
        break;
    }
    case AVMEDIA_TYPE_ATTACHMENT:
    case AVMEDIA_TYPE_UNKNOWN:
        break;
    default:
        AVException::log(AV_LOG_FATAL, "Unknown codec type.");
        throw;
    }

    if (avcodec_parameters_from_context(par, dec_ctx) < 0)
    {
        AVException::log(AV_LOG_ERROR, "Error initializing the decoder context.");
        throw;
    }
}

void InputStream::openDecoder()
{
    int ret;

    if (decoding_needed)
    {
        AVCodec *codec = dec;
        if (!codec)
        {
            std::stringstream ss;
            ss << "Decoder (codec " << avcodec_get_name(dec_ctx->codec_id) << ") not found for input stream " << IdString();
            AVException::log(AV_LOG_FATAL, ss.str().c_str());
            throw;
        }

        dec_ctx->opaque = this;
        dec_ctx->get_format = get_format;
        dec_ctx->get_buffer2 = get_buffer;
        dec_ctx->thread_safe_callbacks = 1;

        av_opt_set_int(dec_ctx, "refcounted_frames", 1, 0);
        if (dec_ctx->codec_id == AV_CODEC_ID_DVB_SUBTITLE && (decoding_needed & DECODING_FOR_OST))
        {
            av_dict_set(&decoder_opts, "compute_edt", "1", AV_DICT_DONT_OVERWRITE);
            if (decoding_needed & DECODING_FOR_FILTER)
                AVException::log(AV_LOG_WARNING,
                                 "Warning using DVB subtitles for filtering and output at the same time is not fully supported, also see -compute_edt [0|1]");
        }

        av_dict_set(&decoder_opts, "sub_text_format", "ass", AV_DICT_DONT_OVERWRITE);

        /* Useful for subtitles retiming by lavf (FIXME), skipping samples in
         * audio, and video decoders such as cuvid or mediacodec */
        dec_ctx->pkt_timebase = st->time_base;

        if (!av_dict_get(decoder_opts, "threads", NULL, 0))
            av_dict_set(&decoder_opts, "threads", "auto", 0);
        /* Attached pics are sparse, therefore we would not want to delay their decoding till EOF. */
        if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
            av_dict_set(&decoder_opts, "threads", "1", 0);

        // set up hardware decoder (if available/specified)
        hw_device_setup_for_decode();

        if ((ret = avcodec_open2(dec_ctx, codec, &decoder_opts)) < 0)
        {
            AVException::log_error(AV_LOG_FATAL, "Error while opening decoder : %s", ret);
            throw;
        }
        assert_avoptions(decoder_opts);
    }

    next_pts = AV_NOPTS_VALUE;
    next_dts = AV_NOPTS_VALUE;
}

/* pkt = NULL means EOF (needed to flush decoder buffers) */
int InputStream::process_input_packet(const AVPacket *pkt, bool no_eof)
{
    int ret = 0;
    bool repeating = false;
    bool eof_reached = false;

    AVPacket avpkt;
    if (!saw_first_ts)
    {
        dts = st->avg_frame_rate.num ? (int64_t)(-dec_ctx->has_b_frames * AV_TIME_BASE / av_q2d(st->avg_frame_rate)) : 0;
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
        next_dts = dts = av_rescale_q(pkt->dts, st->time_base, AV_TIME_BASE_Q);
        if (dec_ctx->codec_type != AVMEDIA_TYPE_VIDEO || !decoding_needed)
            next_pts = pts = dts;
    }

    // while we have more to decode or while the decoder did output something on EOF
    while (decoding_needed)
    {
        int64_t duration_dts = 0;
        int64_t duration_pts = 0;
        int got_output = 0;
        int decode_failed = 0;

        pts = next_pts;
        dts = next_dts;

        switch (dec_ctx->codec_type)
        {
        case AVMEDIA_TYPE_AUDIO:
            ret = decode_audio(repeating ? NULL : &avpkt, &got_output,
                               &decode_failed);
            break;
        case AVMEDIA_TYPE_VIDEO:
            ret = decode_video(repeating ? NULL : &avpkt, &got_output, &duration_pts, !pkt,
                               &decode_failed);
            if (!repeating || !pkt || got_output)
            {
                if (pkt && pkt->duration)
                {
                    duration_dts = av_rescale_q(pkt->duration, st->time_base, AV_TIME_BASE_Q);
                }
                else if (dec_ctx->framerate.num != 0 && dec_ctx->framerate.den != 0)
                {
                    int ticks = av_stream_get_parser(st) ? av_stream_get_parser(st)->repeat_pict + 1 : dec_ctx->ticks_per_frame;
                    duration_dts = ((int64_t)AV_TIME_BASE *
                                    dec_ctx->framerate.den * ticks) /
                                   dec_ctx->framerate.num / dec_ctx->ticks_per_frame;
                }

                if (dts != AV_NOPTS_VALUE && duration_dts)
                {
                    next_dts += duration_dts;
                }
                else
                    next_dts = AV_NOPTS_VALUE;
            }

            if (got_output)
            {
                if (duration_pts > 0)
                {
                    next_pts += av_rescale_q(duration_pts, st->time_base, AV_TIME_BASE_Q);
                }
                else
                {
                    next_pts += duration_dts;
                }
            }
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            if (repeating)
                break;
            ret = transcode_subtitles(&avpkt, &got_output, &decode_failed);
            if (!pkt && ret >= 0)
                ret = AVERROR_EOF;
            break;
        default:
            AVException::log(AV_LOG_FATAL, "Unknown media type for stream #%s", IdString().c_str());
            AVException::throw_last_log();
        }

        if (ret == AVERROR_EOF)
        {
            eof_reached = true;
            break;
        }

        if (ret < 0)
        {
            if (decode_failed)
            {
                AVException::log_error(AV_LOG_ERROR, "Error while decoding stream #%s: %s",
                                       1, IdString().c_str(), ret);
            }
            else
            {
                av_log(NULL, AV_LOG_FATAL, "Error while processing the decoded "
                                           "data for stream #%s",
                       IdString());
            }
            if (!decode_failed || exit_on_error)
                AVException::log_error(AV_LOG_FATAL, "Failed to decode a packet.");
            break;
        }

        if (got_output)
            got_output = 1;

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
    if (!pkt && decoding_needed && eof_reached && !no_eof)
        send_filter_eof();

    /* handle stream copy */
    // if (!decoding_needed && pkt)
    // {
    //     dts = next_dts;
    //     switch (dec_ctx->codec_type)
    //     {
    //     case AVMEDIA_TYPE_AUDIO:
    //         av_assert1(pkt->duration >= 0);
    //         if (dec_ctx->sample_rate)
    //         {
    //             next_dts += ((int64_t)AV_TIME_BASE * dec_ctx->frame_size) /
    //                              dec_ctx->sample_rate;
    //         }
    //         else
    //         {
    //             next_dts += av_rescale_q(pkt->duration, st->time_base, AV_TIME_BASE_Q);
    //         }
    //         break;
    //     case AVMEDIA_TYPE_VIDEO:
    //         if (framerate.num)
    //         {
    //             // TODO: Remove work-around for c99-to-c89 issue 7
    //             AVRational time_base_q = AV_TIME_BASE_Q;
    //             int64_t next_dts = av_rescale_q(next_dts, time_base_q, av_inv_q(framerate));
    //             next_dts = av_rescale_q(next_dts + 1, av_inv_q(framerate), time_base_q);
    //         }
    //         else if (pkt->duration)
    //         {
    //             next_dts += av_rescale_q(pkt->duration, st->time_base, AV_TIME_BASE_Q);
    //         }
    //         else if (dec_ctx->framerate.num != 0)
    //         {
    //             int ticks = av_stream_get_parser(st) ? av_stream_get_parser(st)->repeat_pict + 1 : dec_ctx->ticks_per_frame;
    //             next_dts += ((int64_t)AV_TIME_BASE *
    //                               dec_ctx->framerate.den * ticks) /
    //                              dec_ctx->framerate.num / dec_ctx->ticks_per_frame;
    //         }
    //         break;
    //     }
    //     pts = dts;
    //     next_pts = next_dts;
    // }
    // for (i = 0; i < nb_output_streams; i++)
    // {
    //     OutputStream *ost = output_streams[i];

    //     if (!check_output_constraints(ist, ost) || ost->encoding_needed)
    //         continue;

    //     do_streamcopy(ist, ost, pkt);
    // }

    return ret;
}

int InputStream::decode_audio(AVPacket *pkt, int *got_output, int *decode_failed)
{
    AVCodecContext *avctx = dec_ctx;
    int ret, err = 0;
    AVRational decoded_frame_tb;

    if (!decoded_frame && !(decoded_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    if (!filter_frame && !(filter_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    decoded_frame = decoded_frame;

    // update_benchmark(NULL);
    ret = decode(avctx, decoded_frame, got_output, pkt);
    // update_benchmark("decode_audio %d.%d", file_index, st->index);
    if (ret < 0)
        *decode_failed = 1;

    if (ret >= 0 && avctx->sample_rate <= 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Sample rate %d invalid", avctx->sample_rate);
        ret = AVERROR_INVALIDDATA;
    }

    if (ret != AVERROR_EOF)
        check_decode_result(got_output, ret);

    if (!*got_output || ret < 0)
        return ret;

    samples_decoded += decoded_frame->nb_samples;
    frames_decoded++;

#if 1
    /* increment next_dts to use for the case where the input stream does not
       have timestamps or there are multiple frames in the packet */
    next_pts += ((int64_t)AV_TIME_BASE * decoded_frame->nb_samples) /
                avctx->sample_rate;
    next_dts += ((int64_t)AV_TIME_BASE * decoded_frame->nb_samples) /
                avctx->sample_rate;
#endif

    if (decoded_frame->pts != AV_NOPTS_VALUE)
    {
        decoded_frame_tb = st->time_base;
    }
    else if (pkt && pkt->pts != AV_NOPTS_VALUE)
    {
        decoded_frame->pts = pkt->pts;
        decoded_frame_tb = st->time_base;
    }
    else
    {
        decoded_frame->pts = dts;
        decoded_frame_tb = AV_TIME_BASE_Q;
    }
    if (decoded_frame->pts != AV_NOPTS_VALUE)
        decoded_frame->pts = av_rescale_delta(decoded_frame_tb, decoded_frame->pts,
                                              AVRational({1, avctx->sample_rate}), decoded_frame->nb_samples, &filter_in_rescale_delta_last,
                                              AVRational({1, avctx->sample_rate}));
    nb_samples = decoded_frame->nb_samples;
    err = send_frame_to_filters(decoded_frame);

    av_frame_unref(filter_frame);
    av_frame_unref(decoded_frame);
    return err < 0 ? err : ret;
}

int InputStream::decode_video(AVPacket *pkt, int *got_output, int64_t *duration_pts, int eof, int *decode_failed)
{
    int i, ret = 0, err = 0;
    int64_t best_effort_timestamp;
    int64_t dts = AV_NOPTS_VALUE;
    AVPacket avpkt;

    // With fate-indeo3-2, we're getting 0-sized packets before EOF for some
    // reason. This seems like a semi-critical bug. Don't trigger EOF, and
    // skip the packet.
    if (!eof && pkt && pkt->size == 0)
        return 0;

    if (!decoded_frame && !(decoded_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    if (!filter_frame && !(filter_frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    decoded_frame = decoded_frame;
    if (dts != AV_NOPTS_VALUE)
        dts = av_rescale_q(dts, AV_TIME_BASE_Q, st->time_base);
    if (pkt)
    {
        avpkt = *pkt;
        avpkt.dts = dts; // ffmpeg.c probably shouldn't do this
    }

    // The old code used to set dts on the drain packet, which does not work
    // with the new API anymore.
    if (eof)
    {
        void *new_array = av_realloc_array(dts_buffer, nb_dts_buffer + 1, sizeof(dts_buffer[0]));
        if (!new_array)
            return AVERROR(ENOMEM);
        dts_buffer = (int64_t *)new_array;
        dts_buffer[nb_dts_buffer++] = dts;
    }

    // update_benchmark(NULL);
    ret = decode(dec_ctx, decoded_frame, got_output, pkt ? &avpkt : NULL);
    // update_benchmark("decode_video %d.%d", file_index, st->index);
    if (ret < 0)
        *decode_failed = 1;

    // The following line may be required in some cases where there is no parser
    // or the parser does not has_b_frames correctly
    if (st->codecpar->video_delay < dec_ctx->has_b_frames)
    {
        if (dec_ctx->codec_id == AV_CODEC_ID_H264)
        {
            st->codecpar->video_delay = dec_ctx->has_b_frames;
        }
        else
            av_log(dec_ctx, AV_LOG_WARNING,
                   "video_delay is larger in decoder than demuxer %d > %d."
                   "If you want to help, upload a sample "
                   "of this file to ftp://upload.ffmpeg.org/incoming/ "
                   "and contact the ffmpeg-devel mailing l (ffmpeg-devel@ffmpeg.org)",
                   dec_ctx->has_b_frames,
                   st->codecpar->video_delay);
    }

    if (ret != AVERROR_EOF)
        check_decode_result(got_output, ret);

    if (*got_output && ret >= 0)
    {
        if (dec_ctx->width != decoded_frame->width ||
            dec_ctx->height != decoded_frame->height ||
            dec_ctx->pix_fmt != decoded_frame->format)
        {
            av_log(NULL, AV_LOG_DEBUG, "Frame parameters mismatch context %d,%d,%d != %d,%d,%d",
                   decoded_frame->width,
                   decoded_frame->height,
                   decoded_frame->format,
                   dec_ctx->width,
                   dec_ctx->height,
                   dec_ctx->pix_fmt);
        }
    }

    if (!*got_output || ret < 0)
        return ret;

    if (top_field_first >= 0)
        decoded_frame->top_field_first = top_field_first;

    frames_decoded++;

    if (hwaccel_retrieve_data && decoded_frame->format == hwaccel_pix_fmt)
    {
        err = hwaccel_retrieve_data(dec_ctx, decoded_frame);
        if (err < 0)
            goto fail;
    }
    hwaccel_retrieved_pix_fmt = AVPixelFormat(decoded_frame->format);

    best_effort_timestamp = decoded_frame->best_effort_timestamp;
    *duration_pts = decoded_frame->pkt_duration;

    if (framerate.num)
        best_effort_timestamp = cfr_next_pts++;

    if (eof && best_effort_timestamp == AV_NOPTS_VALUE && nb_dts_buffer > 0)
    {
        best_effort_timestamp = dts_buffer[0];

        for (i = 0; i < nb_dts_buffer - 1; i++)
            dts_buffer[i] = dts_buffer[i + 1];
        nb_dts_buffer--;
    }

    if (best_effort_timestamp != AV_NOPTS_VALUE)
    {
        int64_t ts = av_rescale_q(decoded_frame->pts = best_effort_timestamp, st->time_base, AV_TIME_BASE_Q);

        if (ts != AV_NOPTS_VALUE)
            next_pts = pts = ts;
    }

    // if (debug_ts)
    // {
    //     av_log(NULL, AV_LOG_INFO, "decoder -> ist_index:%d type:video "
    //                               "frame_pts:%s frame_pts_time:%s best_effort_ts:%" PRId64 " best_effort_ts_time:%s keyframe:%d frame_type:%d time_base:%d/%d",
    //            st->index, av_ts2str(decoded_frame->pts),
    //            av_ts2timestr(decoded_frame->pts, &st->time_base),
    //            best_effort_timestamp,
    //            av_ts2timestr(best_effort_timestamp, &st->time_base),
    //            decoded_frame->key_frame, decoded_frame->pict_type,
    //            st->time_base.num, st->time_base.den);
    // }

    if (st->sample_aspect_ratio.num)
        decoded_frame->sample_aspect_ratio = st->sample_aspect_ratio;

    err = send_frame_to_filters(decoded_frame);

fail:
    av_frame_unref(filter_frame);
    av_frame_unref(decoded_frame);
    return err < 0 ? err : ret;
}

int InputStream::transcode_subtitles(AVPacket *pkt, int *got_output, int *decode_failed)
{
    AVSubtitle subtitle;
    int free_sub = 1;
    int i, ret = avcodec_decode_subtitle2(dec_ctx,
                                          &subtitle, got_output, pkt);

    check_decode_result(got_output, ret);

    if (ret < 0 || !*got_output)
    {
        *decode_failed = 1;
        if (!pkt->size)
            sub2video_flush();
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
                       "Subtitle duration reduced from %" PRId32 " to %d%s",
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

    if (sub2video.frame)
    {
        sub2video_update(&subtitle);
    }
    else if (filters.size())
    {
        if (!sub2video.sub_queue)
            sub2video.sub_queue = av_fifo_alloc(8 * sizeof(AVSubtitle));
        if (!sub2video.sub_queue)
            AVException::log_error(AV_LOG_FATAL, "No subtitle queue is found.");
        if (!av_fifo_space(sub2video.sub_queue))
        {
            ret = av_fifo_realloc2(sub2video.sub_queue, 2 * av_fifo_size(sub2video.sub_queue));
            if (ret < 0)
                AVException::log_error(AV_LOG_FATAL, "Failed to allocate FIFO buffer for subtitle queue.");
        }
        av_fifo_generic_write(sub2video.sub_queue, &subtitle, sizeof(subtitle), NULL);
        free_sub = 0;
    }

    if (!subtitle.num_rects)
        goto out;

    frames_decoded++;

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

void InputStream::send_filter_eof()
{
    int i, ret;
    /* TODO keep pts also in stream time base to avoid converting back */
    int64_t pts_rescaled = av_rescale_q_rnd(pts, AV_TIME_BASE_Q, st->time_base,
                                            AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

    for (i = 0; i < filters.size(); i++)
    {
        ret = ifilter_send_eof(filters[i], pts_rescaled);
        if (ret < 0)
            AVException::log(AV_LOG_FATAL, "Error marking filters as finished.");
    }
}

void InputStream::sub2video_heartbeat(int64_t pts)
{
    int j, nb_reqs;
    int64_t pts2;

    /* When a frame is read from a file, examine all sub2video streams in
       the same file and send the sub2video frame again. Otherwise, decoded
       video frames could be accumulating in the filter graph while a filter
       (possibly overlay) is desperately waiting for a subtitle frame. */
    // for (i = 0; i < infile->nb_streams; i++)
    for (auto ist2 = file.streams.begin(); ist2 != file.streams.end(); ++ist2)
    {
        if (!ist2->sub2video.frame)
            continue;
        /* subtitles seem to be usually muxed ahead of other streams;
           if not, subtracting a larger time here is necessary */
        pts2 = av_rescale_q(pts, st->time_base, ist2->st->time_base) - 1;
        /* do not send the heartbeat frame if the subtitle is already ahead */
        if (pts2 <= ist2->sub2video.last_pts)
            continue;
        if (pts2 >= ist2->sub2video.end_pts ||
            (!ist2->sub2video.frame->data[0] && ist2->sub2video.end_pts < INT64_MAX))
            ist2->sub2video_update(NULL);
        for (j = 0, nb_reqs = 0; j < ist2->filters.size(); j++)
            nb_reqs += av_buffersrc_get_nb_failed_requests(ist2->filters[j]->filter);
        if (nb_reqs)
            ist2->sub2video_push_ref(pts2);
    }
}

void InputStream::check_decode_result(int *got_output, int ret)
{
    if (*got_output || ret < 0)
        decode_error_stat[ret < 0]++;

    if (ret < 0 && exit_on_error)
        AVException::throw_last_log();

    if (*got_output && dec_ctx->codec_type != AVMEDIA_TYPE_SUBTITLE)
    {
        if (decoded_frame->decode_error_flags || (decoded_frame->flags & AV_FRAME_FLAG_CORRUPT))
        {
            AVException::log(exit_on_error ? AV_LOG_FATAL : AV_LOG_WARNING,
                             "%s: corrupt decoded frame in stream %d\n", file.ctx->url, st->index);
            if (exit_on_error)
                AVException::throw_last_log();
        }
    }
}

int InputStream::send_frame_to_filters(AVFrame *decoded_frame)
{
    int i, ret;
    AVFrame *f;

    av_assert1(filters.size() > 0); /* ensure ret is initialized */
    for (i = 0; i < filters.size(); i++)
    {
        if (i < filters.size() - 1)
        {
            f = filter_frame;
            ret = av_frame_ref(f, decoded_frame);
            if (ret < 0)
                break;
        }
        else
            f = decoded_frame;
        ret = ifilter_send_frame(filters[i], f);
        if (ret == AVERROR_EOF)
            ret = 0; /* ignore */
        if (ret < 0)
        {
            AVException::log_error(AV_LOG_ERROR,
                                   "Failed to inject frame into filter network: %s", ret);
            break;
        }
    }
    return ret;
}

void InputStream::sub2video_flush()
{
    int i;
    int ret;

    if (sub2video.end_pts < INT64_MAX)
        sub2video_update(NULL);
    for (i = 0; i < filters.size(); i++)
    {
        ret = av_buffersrc_add_frame(filters[i]->filter, NULL);
        if (ret != AVERROR_EOF && ret < 0)
            av_log(NULL, AV_LOG_WARNING, "Flush the frame error.");
    }
}

void InputStream::sub2video_push_ref(int64_t pts)
{
    AVFrame *frame = sub2video.frame;
    int i;
    int ret;

    av_assert1(frame->data[0]);
    sub2video.last_pts = frame->pts = pts;
    for (i = 0; i < filters.size(); i++)
    {
        ret = av_buffersrc_add_frame_flags(filters[i]->filter, frame,
                                           AV_BUFFERSRC_FLAG_KEEP_REF |
                                               AV_BUFFERSRC_FLAG_PUSH);
        if (ret != AVERROR_EOF && ret < 0)
            AVException::log_error(AV_LOG_WARNING, "Error while add the frame to buffer source(%s).",
                                   ret);
    }
}

void InputStream::sub2video_update(AVSubtitle *sub)
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
        av_log(dec_ctx, AV_LOG_ERROR,
               "Impossible to get a blank canvas.");
        return;
    }
    dst = (int8_t *)frame->data[0];
    dst_linesize = frame->linesize[0];
    for (i = 0; i < num_rects; i++)
        sub2video_copy_rect((uint8_t *)dst, dst_linesize, frame->width, frame->height, sub->rects[i]);
    sub2video_push_ref(pts);
    sub2video.end_pts = end_pts;
}

/////////////

AVPixelFormat InputStream::get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts)
{
    InputStream &ist = *(InputStream *)s->opaque;
    const enum AVPixelFormat *p;
    int ret;

    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
    {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        const AVCodecHWConfig *config = NULL;
        int i;

        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            break;

        if (ist.hwaccel_id == HWACCEL_GENERIC ||
            ist.hwaccel_id == HWACCEL_AUTO)
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
            if (config->device_type != ist.hwaccel_device_type)
            {
                // Different hwaccel offered, ignore.
                continue;
            }

            ret = hwaccel_decode_init(s);
            if (ret < 0)
            {
                if (ist.hwaccel_id == HWACCEL_GENERIC)
                {
                    av_log(NULL, AV_LOG_FATAL,
                           "%s hwaccel requested for input stream %s, "
                           "but cannot be initialized.",
                           av_hwdevice_get_type_name(config->device_type),
                           ist.IdString().c_str());
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
            if (hwaccel->id != ist.hwaccel_id)
            {
                // Does not match requested hwaccel.
                continue;
            }

            ret = hwaccel->init(s);
            if (ret < 0)
            {
                av_log(NULL, AV_LOG_FATAL,
                       "%s hwaccel requested for input stream %s, "
                       "but cannot be initialized.",
                       hwaccel->name,
                       ist.IdString().c_str());
                return AV_PIX_FMT_NONE;
            }
        }

        if (ist.hw_frames_ctx)
        {
            s->hw_frames_ctx = av_buffer_ref(ist.hw_frames_ctx);
            if (!s->hw_frames_ctx)
                return AV_PIX_FMT_NONE;
        }

        ist.hwaccel_pix_fmt = *p;
        break;
    }

    return *p;
}

int InputStream::get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    InputStream &ist = *(InputStream *)s->opaque;

    if (ist.hwaccel_get_buffer && frame->format == ist.hwaccel_pix_fmt)
        return ist.hwaccel_get_buffer(s, frame, flags);

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

int InputStream::sub2video_get_blank_frame()
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

void sub2video_copy_rect(uint8_t *dst, int dst_linesize, int w, int h, AVSubtitleRect *r)
{
    uint32_t *pal, *dst2;
    uint8_t *src, *src2;
    int x, y;

    if (r->type != SUBTITLE_BITMAP)
    {
        av_log(NULL, AV_LOG_WARNING, "sub2video: non-bitmap subtitle");
        return;
    }
    if (r->x < 0 || r->x + r->w > w || r->y < 0 || r->y + r->h > h)
    {
        av_log(NULL, AV_LOG_WARNING, "sub2video: rectangle (%d %d %d %d) overflowing %d %d",
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

void InputStream::hw_device_setup_for_decode()
{
    const AVCodecHWConfig *config;
    AVHWDeviceType type;
    HWDevice *dev = NULL;
    int err, auto_device = 0;

    if (hwaccel_device)
    {
        dev = hw_device_get_by_name(hwaccel_device);
        if (!dev)
        {
            if (hwaccel_id == HWACCEL_AUTO)
            {
                auto_device = 1;
            }
            else if (hwaccel_id == HWACCEL_GENERIC)
            {
                type = hwaccel_device_type;
                err = hw_device_init_from_type(type, hwaccel_device,
                                               &dev);
            }
            else
            {
                // This will be dealt with by API-specific initialisation
                // (using hwaccel_device), so nothing further needed here.
                return;
            }
        }
        else
        {
            if (hwaccel_id == HWACCEL_AUTO)
            {
                hwaccel_device_type = dev->type;
            }
            else if (hwaccel_device_type != dev->type)
            {
                std::stringstream ss;
                av_log(dec_ctx, AV_LOG_ERROR, "Invalid hwaccel device "
                                              "specified for decoder %s: device %s of type %s is not "
                                              "usable with hwaccel %s.",
                       IdString().c_str(), dev->name,
                       av_hwdevice_get_type_name(dev->type),
                       av_hwdevice_get_type_name(hwaccel_device_type));
                throw; //AVERROR(EINVAL);
            }
        }
    }
    else
    {
        if (hwaccel_id == HWACCEL_AUTO)
        {
            auto_device = 1;
        }
        else if (hwaccel_id == HWACCEL_GENERIC)
        {
            type = hwaccel_device_type;
            dev = hw_device_get_by_type(type);
            if (!dev)
                err = hw_device_init_from_type(type, NULL, &dev);
        }
        else
        {
            dev = hw_device_match_by_codec(dec);
            if (!dev)
            {
                // No device for this codec, but not using generic hwaccel
                // and therefore may well not need one - ignore.
                return;
            }
        }
    }

    if (auto_device)
    {
        int i;
        if (!avcodec_get_hw_config(dec, 0))
        {
            // Decoder does not support any hardware devices.
            return;
        }
        for (i = 0; !dev; i++)
        {
            config = avcodec_get_hw_config(dec, i);
            if (!config)
                break;
            type = config->device_type;
            dev = hw_device_get_by_type(type);
            if (dev)
            {
                av_log(dec_ctx, AV_LOG_INFO, "Using auto "
                                             "hwaccel type %s with existing device %s.",
                       av_hwdevice_get_type_name(type), dev->name);
            }
        }
        for (i = 0; !dev; i++)
        {
            config = avcodec_get_hw_config(dec, i);
            if (!config)
                break;
            type = config->device_type;
            // Try to make a new device of this type.
            err = hw_device_init_from_type(type, hwaccel_device, &dev);
            if (err < 0)
            {
                // Can't make a device of this type.
                continue;
            }
            if (hwaccel_device)
            {
                av_log(dec_ctx, AV_LOG_INFO, "Using auto "
                                             "hwaccel type %s with new device created "
                                             "from %s.",
                       av_hwdevice_get_type_name(type),
                       hwaccel_device);
            }
            else
            {
                av_log(dec_ctx, AV_LOG_INFO, "Using auto "
                                             "hwaccel type %s with new default device.",
                       av_hwdevice_get_type_name(type));
            }
        }
        if (dev)
        {
            hwaccel_device_type = type;
        }
        else
        {
            av_log(dec_ctx, AV_LOG_INFO, "Auto hwaccel "
                                         "disabled: no device found.");
            hwaccel_id = HWACCEL_NONE;
            return;
        }
    }

    if (!dev)
    {
        av_log(dec_ctx, AV_LOG_ERROR, "No device available "
                                      "for decoder %s: device type %s needed for codec %s.",
               IdString().c_str(), av_hwdevice_get_type_name(type), dec->name);
        throw; //err;
    }

    dec_ctx->hw_device_ctx = av_buffer_ref(dev->device_ref);
    if (!dec_ctx->hw_device_ctx)
        throw; // AVERROR(ENOMEM);

    return;
}
