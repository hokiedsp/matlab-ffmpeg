#include "transcode_outputstream.h"

extern "C"
{
#include <libavutil/display.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/avassert.h>
}

#include "../ffmpeg/avexception.h"
#include "transcode_inputstream.h"
#include "transcode_outputfile.h"
#include "transcode_hw.h"
#include "transcode_utils.h"

InputStream **input_streams = NULL;
OutputFile **output_files = NULL;
OutputStream **output_streams = NULL;
int nb_output_streams = 0;
#define VSYNC_AUTO -1
#define VSYNC_PASSTHROUGH 0
#define VSYNC_CFR 1
#define VSYNC_VFR 2
#define VSYNC_VSCFR 0xfe
#define VSYNC_DROP 0xff
int video_sync_method = VSYNC_AUTO;
int frame_bits_per_raw_sample = 0;
int copy_tb = -1;
int audio_volume = 256;

const char *const forced_keyframes_const_names[] = {
    "n",
    "n_forced",
    "prev_forced_n",
    "prev_forced_t",
    "t",
    NULL};

// output stream
int init_output_stream_encode(OutputStream *ost);
InputStream *get_input_stream(OutputStream *ost);
int init_output_stream_streamcopy(OutputStream *ost);
int init_output_bsfs(OutputStream *ost);
void init_encoder_time_base(OutputStream *ost, AVRational default_time_base);
void parse_forced_key_frames(char *kf, OutputStream *ost, AVCodecContext *avctx);
void close_all_output_streams(OutputStream *ost, OSTFinished this_stream, OSTFinished others);
int check_recording_time(OutputStream *ost);

int compare_int64(const void *a, const void *b);

int init_output_stream(OutputStream *ost, char *error, int error_len)
{
    int ret = 0;

    if (ost->encoding_needed)
    {
        AVCodec *codec = ost->enc;
        AVCodecContext *dec = NULL;
        InputStream *ist;

        ret = init_output_stream_encode(ost);
        if (ret < 0)
            return ret;

        if ((ist = get_input_stream(ost)))
            dec = ist->dec_ctx;
        if (dec && dec->subtitle_header)
        {
            /* ASS code assumes this buffer is null terminated so add extra byte. */
            ost->enc_ctx->subtitle_header = (uint8_t *)av_mallocz(dec->subtitle_header_size + 1);
            if (!ost->enc_ctx->subtitle_header)
                return AVERROR(ENOMEM);
            memcpy(ost->enc_ctx->subtitle_header, dec->subtitle_header, dec->subtitle_header_size);
            ost->enc_ctx->subtitle_header_size = dec->subtitle_header_size;
        }
        if (!av_dict_get(ost->encoder_opts, "threads", NULL, 0))
            av_dict_set(&ost->encoder_opts, "threads", "auto", 0);
        if (ost->enc->type == AVMEDIA_TYPE_AUDIO &&
            !codec->defaults &&
            !av_dict_get(ost->encoder_opts, "b", NULL, 0) &&
            !av_dict_get(ost->encoder_opts, "ab", NULL, 0))
            av_dict_set(&ost->encoder_opts, "b", "128000", 0);

        if (ost->filter && av_buffersink_get_hw_frames_ctx(ost->filter->filter) &&
            ((AVHWFramesContext *)av_buffersink_get_hw_frames_ctx(ost->filter->filter)->data)->format ==
                av_buffersink_get_format(ost->filter->filter))
        {
            ost->enc_ctx->hw_frames_ctx = av_buffer_ref(av_buffersink_get_hw_frames_ctx(ost->filter->filter));
            if (!ost->enc_ctx->hw_frames_ctx)
                return AVERROR(ENOMEM);
        }
        else
        {
            ret = hw_device_setup_for_encode(ost);
            if (ret < 0)
            {
                AVException::sprintf_error(error, error_len, "Device setup failed for "
                                                             "encoder on output stream #%d:%d : %s",
                                           2,
                                           ost->file_index, ost->index, ret);
                return ret;
            }
        }
        if (ist && ist->dec->type == AVMEDIA_TYPE_SUBTITLE && ost->enc->type == AVMEDIA_TYPE_SUBTITLE)
        {
            int input_props = 0, output_props = 0;
            AVCodecDescriptor const *input_descriptor =
                avcodec_descriptor_get(dec->codec_id);
            AVCodecDescriptor const *output_descriptor =
                avcodec_descriptor_get(ost->enc_ctx->codec_id);
            if (input_descriptor)
                input_props = input_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
            if (output_descriptor)
                output_props = output_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
            if (input_props && output_props && input_props != output_props)
            {
                snprintf(error, error_len,
                         "Subtitle encoding currently only possible from text to text "
                         "or bitmap to bitmap");
                return AVERROR_INVALIDDATA;
            }
        }

        if ((ret = avcodec_open2(ost->enc_ctx, codec, &ost->encoder_opts)) < 0)
        {
            snprintf(error, error_len,
                     "Error while opening encoder for output stream #%d:%d - "
                     "maybe incorrect parameters such as bit_rate, rate, width or height",
                     ost->file_index, ost->index);
            return ret;
        }
        if (ost->enc->type == AVMEDIA_TYPE_AUDIO &&
            !(ost->enc->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
            av_buffersink_set_frame_size(ost->filter->filter,
                                         ost->enc_ctx->frame_size);
        assert_avoptions(ost->encoder_opts);
        if (ost->enc_ctx->bit_rate && ost->enc_ctx->bit_rate < 1000 &&
            ost->enc_ctx->codec_id != AV_CODEC_ID_CODEC2 /* don't complain about 700 bit/s modes */)
            av_log(NULL, AV_LOG_WARNING, "The bitrate parameter is set too low."
                                         " It takes bits/s as argument, not kbits/s\n");

        ret = avcodec_parameters_from_context(ost->st->codecpar, ost->enc_ctx);
        if (ret < 0)
        {
            AVException::log(AV_LOG_FATAL,
                             "Error initializing the output stream codec context.\n");
            throw;
        }
        /*
         * FIXME: ost->st->codec should't be needed here anymore.
         */
        // ret = avcodec_copy_context(ost->st->codec, ost->enc_ctx);
        // AVCodecParameters par;
        // avcodec_parameters_from_context(&par, ost->enc_ctx);
        // avcodec_parameters_to_context(ost->st->codec, &par);
        if (ret < 0)
            return ret;

        if (ost->enc_ctx->nb_coded_side_data)
        {
            int i;

            for (i = 0; i < ost->enc_ctx->nb_coded_side_data; i++)
            {
                const AVPacketSideData *sd_src = &ost->enc_ctx->coded_side_data[i];
                uint8_t *dst_data;

                dst_data = av_stream_new_side_data(ost->st, sd_src->type, sd_src->size);
                if (!dst_data)
                    return AVERROR(ENOMEM);
                memcpy(dst_data, sd_src->data, sd_src->size);
            }
        }

        /*
         * Add global input side data. For now this is naive, and copies it
         * from the input stream's global side data. All side data should
         * really be funneled over AVFrame and libavfilter, then added back to
         * packet side data, and then potentially using the first packet for
         * global side data.
         */
        if (ist)
        {
            int i;
            for (i = 0; i < ist->st->nb_side_data; i++)
            {
                AVPacketSideData *sd = &ist->st->side_data[i];
                uint8_t *dst = av_stream_new_side_data(ost->st, sd->type, sd->size);
                if (!dst)
                    return AVERROR(ENOMEM);
                memcpy(dst, sd->data, sd->size);
                if (ist->autorotate && sd->type == AV_PKT_DATA_DISPLAYMATRIX)
                    av_display_rotation_set((int32_t *)dst, 0);
            }
        }

        // copy timebase while removing common factors
        if (ost->st->time_base.num <= 0 || ost->st->time_base.den <= 0)
            ost->st->time_base = av_add_q(ost->enc_ctx->time_base, AVRational({0, 1}));

        // copy estimated duration as a hint to the muxer
        if (ost->st->duration <= 0 && ist && ist->st->duration > 0)
            ost->st->duration = av_rescale_q(ist->st->duration, ist->st->time_base, ost->st->time_base);

        // ost->st->codec->codec = ost->enc_ctx->codec;
    }
    else if (ost->stream_copy)
    {
        ret = init_output_stream_streamcopy(ost);
        if (ret < 0)
            return ret;
    }

    // parse user provided disposition, and update stream values
    if (ost->disposition)
    {
        static const AVOption opts[] = {
            {"disposition", NULL, 0, AV_OPT_TYPE_FLAGS, {0}, INT64_MIN, INT64_MAX, 0, "flags"},
            {"default", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_DEFAULT}, 0, 0, 0, "flags"},
            {"dub", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_DUB}, 0, 0, 0, "flags"},
            {"original", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_ORIGINAL}, 0, 0, 0, "flags"},
            {"comment", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_COMMENT}, 0, 0, 0, "flags"},
            {"lyrics", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_LYRICS}, 0, 0, 0, "flags"},
            {"karaoke", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_KARAOKE}, 0, 0, 0, "flags"},
            {"forced", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_FORCED}, 0, 0, 0, "flags"},
            {"hearing_impaired", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_HEARING_IMPAIRED}, 0, 0, 0, "flags"},
            {"visual_impaired", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_VISUAL_IMPAIRED}, 0, 0, 0, "flags"},
            {"clean_effects", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_CLEAN_EFFECTS}, 0, 0, 0, "flags"},
            {"attached_pic", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_ATTACHED_PIC}, 0, 0, 0, "flags"},
            {"captions", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_CAPTIONS}, 0, 0, 0, "flags"},
            {"descriptions", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_DESCRIPTIONS}, 0, 0, 0, "flags"},
            {"dependent", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_DEPENDENT}, 0, 0, 0, "flags"},
            {"metadata", NULL, 0, AV_OPT_TYPE_CONST, {AV_DISPOSITION_METADATA}, 0, 0, 0, "flags"},
            {"", NULL, 0, AV_OPT_TYPE_CONST, {0}, 0, 0, 0, ""},
        };
        static const AVClass av_class = {"", av_default_item_name, opts, LIBAVUTIL_VERSION_INT,
                                         0, 0, NULL, NULL, AV_CLASS_CATEGORY_NA, NULL, NULL};

        const AVClass *pclass = &av_class;

        ret = av_opt_eval_flags(&pclass, &opts[0], ost->disposition, &ost->st->disposition);
        if (ret < 0)
            return ret;
    }

    /* initialize bitstream filters for the output stream
     * needs to be done here, because the codec id for streamcopy is not
     * known until now */
    ret = init_output_bsfs(ost);
    if (ret < 0)
        return ret;

    ost->initialized = 1;

    ret = check_init_output_file(output_files[ost->file_index], ost->file_index);
    if (ret < 0)
        return ret;

    return ret;
}

void close_output_stream(OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];

    ost->finished = (OSTFinished)((int)ost->finished | (int)ENCODER_FINISHED);
    if (of->shortest)
    {
        int64_t end = av_rescale_q(ost->sync_opts - ost->first_pts, ost->enc_ctx->time_base, AV_TIME_BASE_Q);
        of->recording_time = FFMIN(of->recording_time, end);
    }
}

void finish_output_stream(OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];
    int i;

    ost->finished = (OSTFinished)((int)ENCODER_FINISHED | (int)MUXER_FINISHED);

    if (of->shortest)
    {
        for (i = 0; i < of->ctx->nb_streams; i++)
            output_streams[of->ost_index + i]->finished = (OSTFinished)((int)ENCODER_FINISHED | (int)MUXER_FINISHED);
    }
}

int init_output_stream_encode(OutputStream *ost)
{
    InputStream *ist = get_input_stream(ost);
    AVCodecContext *enc_ctx = ost->enc_ctx;
    AVCodecContext *dec_ctx = NULL;
    AVFormatContext *oc = output_files[ost->file_index]->ctx;
    int j, ret;

    set_encoder_id(output_files[ost->file_index], ost);

    // Muxers use AV_PKT_DATA_DISPLAYMATRIX to signal rotation. On the other
    // hand, the legacy API makes demuxers set "rotate" metadata entries,
    // which have to be filtered out to prevent leaking them to output files.
    av_dict_set(&ost->st->metadata, "rotate", NULL, 0);

    if (ist)
    {
        ost->st->disposition = ist->st->disposition;

        dec_ctx = ist->dec_ctx;

        enc_ctx->chroma_sample_location = dec_ctx->chroma_sample_location;
    }
    else
    {
        for (j = 0; j < oc->nb_streams; j++)
        {
            AVStream *st = oc->streams[j];
            if (st != ost->st && st->codecpar->codec_type == ost->st->codecpar->codec_type)
                break;
        }
        if (j == oc->nb_streams)
            if (ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO ||
                ost->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                ost->st->disposition = AV_DISPOSITION_DEFAULT;
    }

    if (enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        if (!ost->frame_rate.num)
            ost->frame_rate = av_buffersink_get_frame_rate(ost->filter->filter);
        if (ist && !ost->frame_rate.num)
            ost->frame_rate = ist->framerate;
        if (ist && !ost->frame_rate.num)
            ost->frame_rate = ist->st->r_frame_rate;
        if (ist && !ost->frame_rate.num)
        {
            ost->frame_rate = AVRational({25, 1});
            av_log(NULL, AV_LOG_WARNING,
                   "No information "
                   "about the input framerate is available. Falling "
                   "back to a default value of 25fps for output stream #%d:%d. Use the -r option "
                   "if you want a different framerate.\n",
                   ost->file_index, ost->index);
        }
        //      ost->frame_rate = ist->st->avg_frame_rate.num ? ist->st->avg_frame_rate : AVRational({25, 1});
        if (ost->enc->supported_framerates && !ost->force_fps)
        {
            int idx = av_find_nearest_q_idx(ost->frame_rate, ost->enc->supported_framerates);
            ost->frame_rate = ost->enc->supported_framerates[idx];
        }
        // reduce frame rate for mpeg4 to be within the spec limits
        if (enc_ctx->codec_id == AV_CODEC_ID_MPEG4)
        {
            av_reduce(&ost->frame_rate.num, &ost->frame_rate.den,
                      ost->frame_rate.num, ost->frame_rate.den, 65535);
        }
    }

    switch (enc_ctx->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        enc_ctx->sample_fmt = (AVSampleFormat)av_buffersink_get_format(ost->filter->filter);
        if (dec_ctx)
            enc_ctx->bits_per_raw_sample = FFMIN(dec_ctx->bits_per_raw_sample,
                                                 av_get_bytes_per_sample(enc_ctx->sample_fmt) << 3);
        enc_ctx->sample_rate = av_buffersink_get_sample_rate(ost->filter->filter);
        enc_ctx->channel_layout = av_buffersink_get_channel_layout(ost->filter->filter);
        enc_ctx->channels = av_buffersink_get_channels(ost->filter->filter);

        init_encoder_time_base(ost, av_make_q(1, enc_ctx->sample_rate));
        break;

    case AVMEDIA_TYPE_VIDEO:
        init_encoder_time_base(ost, av_inv_q(ost->frame_rate));

        if (!(enc_ctx->time_base.num && enc_ctx->time_base.den))
            enc_ctx->time_base = av_buffersink_get_time_base(ost->filter->filter);
        if (av_q2d(enc_ctx->time_base) < 0.001 && video_sync_method != VSYNC_PASSTHROUGH && (video_sync_method == VSYNC_CFR || video_sync_method == VSYNC_VSCFR || (video_sync_method == VSYNC_AUTO && !(oc->oformat->flags & AVFMT_VARIABLE_FPS))))
        {
            av_log(oc, AV_LOG_WARNING, "Frame rate very high for a muxer not efficiently supporting it.\n"
                                       "Please consider specifying a lower framerate, a different muxer or -vsync 2\n");
        }
        for (j = 0; j < ost->forced_kf_count; j++)
            ost->forced_kf_pts[j] = av_rescale_q(ost->forced_kf_pts[j],
                                                 AV_TIME_BASE_Q,
                                                 enc_ctx->time_base);

        enc_ctx->width = av_buffersink_get_w(ost->filter->filter);
        enc_ctx->height = av_buffersink_get_h(ost->filter->filter);
        enc_ctx->sample_aspect_ratio = ost->st->sample_aspect_ratio =
            ost->frame_aspect_ratio.num ? // overridden by the -aspect cli option
                av_mul_q(ost->frame_aspect_ratio, AVRational({enc_ctx->height, enc_ctx->width}))
                                        : av_buffersink_get_sample_aspect_ratio(ost->filter->filter);

        enc_ctx->pix_fmt = (AVPixelFormat)av_buffersink_get_format(ost->filter->filter);
        if (dec_ctx)
            enc_ctx->bits_per_raw_sample = FFMIN(dec_ctx->bits_per_raw_sample,
                                                 av_pix_fmt_desc_get(enc_ctx->pix_fmt)->comp[0].depth);

        enc_ctx->framerate = ost->frame_rate;

        ost->st->avg_frame_rate = ost->frame_rate;

        if (!dec_ctx ||
            enc_ctx->width != dec_ctx->width ||
            enc_ctx->height != dec_ctx->height ||
            enc_ctx->pix_fmt != dec_ctx->pix_fmt)
        {
            enc_ctx->bits_per_raw_sample = frame_bits_per_raw_sample;
        }

        if (ost->top_field_first == 0)
        {
            enc_ctx->field_order = AV_FIELD_BB;
        }
        else if (ost->top_field_first == 1)
        {
            enc_ctx->field_order = AV_FIELD_TT;
        }

        if (ost->forced_keyframes)
        {
            if (!strncmp(ost->forced_keyframes, "expr:", 5))
            {
                ret = av_expr_parse(&ost->forced_keyframes_pexpr, ost->forced_keyframes + 5,
                                    forced_keyframes_const_names, NULL, NULL, NULL, NULL, 0, NULL);
                if (ret < 0)
                {
                    av_log(NULL, AV_LOG_ERROR,
                           "Invalid force_key_frames expression '%s'\n", ost->forced_keyframes + 5);
                    return ret;
                }
                ost->forced_keyframes_expr_const_values[FKF_N] = 0;
                ost->forced_keyframes_expr_const_values[FKF_N_FORCED] = 0;
                ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_N] = NAN;
                ost->forced_keyframes_expr_const_values[FKF_PREV_FORCED_T] = NAN;

                // Don't parse the 'forced_keyframes' in case of 'keep-source-keyframes',
                // parse it only for static kf timings
            }
            else if (strncmp(ost->forced_keyframes, "source", 6))
            {
                parse_forced_key_frames(ost->forced_keyframes, ost, ost->enc_ctx);
            }
        }
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        enc_ctx->time_base = AV_TIME_BASE_Q;
        if (!enc_ctx->width)
        {
            enc_ctx->width = input_streams[ost->source_index]->st->codecpar->width;
            enc_ctx->height = input_streams[ost->source_index]->st->codecpar->height;
        }
        break;
    case AVMEDIA_TYPE_DATA:
        break;
    default:
        abort();
        break;
    }

    ost->mux_timebase = enc_ctx->time_base;

    return 0;
}

InputStream *get_input_stream(OutputStream *ost)
{
    if (ost->source_index >= 0)
        return input_streams[ost->source_index];
    return NULL;
}

int init_output_stream_streamcopy(OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];
    InputStream *ist = get_input_stream(ost);
    AVCodecParameters *par_dst = ost->st->codecpar;
    AVCodecParameters *par_src = ost->ref_par;
    AVRational sar;
    int i, ret;
    uint32_t codec_tag = par_dst->codec_tag;

    av_assert0(ist && !ost->filter);

    ret = avcodec_parameters_to_context(ost->enc_ctx, ist->st->codecpar);
    if (ret >= 0)
        ret = av_opt_set_dict(ost->enc_ctx, &ost->encoder_opts);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_FATAL,
               "Error setting up codec context options.\n");
        return ret;
    }

    ret = avcodec_parameters_from_context(par_src, ost->enc_ctx);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_FATAL,
               "Error getting reference codec parameters.\n");
        return ret;
    }

    if (!codec_tag)
    {
        unsigned int codec_tag_tmp;
        if (!of->ctx->oformat->codec_tag ||
            av_codec_get_id(of->ctx->oformat->codec_tag, par_src->codec_tag) == par_src->codec_id ||
            !av_codec_get_tag2(of->ctx->oformat->codec_tag, par_src->codec_id, &codec_tag_tmp))
            codec_tag = par_src->codec_tag;
    }

    ret = avcodec_parameters_copy(par_dst, par_src);
    if (ret < 0)
        return ret;

    par_dst->codec_tag = codec_tag;

    if (!ost->frame_rate.num)
        ost->frame_rate = ist->framerate;
    ost->st->avg_frame_rate = ost->frame_rate;

    ret = avformat_transfer_internal_stream_timing_info(of->ctx->oformat, ost->st, ist->st, (AVTimebaseSource)copy_tb);
    if (ret < 0)
        return ret;

    // copy timebase while removing common factors
    if (ost->st->time_base.num <= 0 || ost->st->time_base.den <= 0)
        ost->st->time_base = av_add_q(av_stream_get_codec_timebase(ost->st), AVRational({0, 1}));

    // copy estimated duration as a hint to the muxer
    if (ost->st->duration <= 0 && ist->st->duration > 0)
        ost->st->duration = av_rescale_q(ist->st->duration, ist->st->time_base, ost->st->time_base);

    // copy disposition
    ost->st->disposition = ist->st->disposition;

    if (ist->st->nb_side_data)
    {
        for (i = 0; i < ist->st->nb_side_data; i++)
        {
            const AVPacketSideData *sd_src = &ist->st->side_data[i];
            uint8_t *dst_data;

            dst_data = av_stream_new_side_data(ost->st, sd_src->type, sd_src->size);
            if (!dst_data)
                return AVERROR(ENOMEM);
            memcpy(dst_data, sd_src->data, sd_src->size);
        }
    }

    if (ost->rotate_overridden)
    {
        uint8_t *sd = av_stream_new_side_data(ost->st, AV_PKT_DATA_DISPLAYMATRIX,
                                              sizeof(int32_t) * 9);
        if (sd)
            av_display_rotation_set((int32_t *)sd, -ost->rotate_override_value);
    }

    switch (par_dst->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        if (audio_volume != 256)
        {
            AVException::log(AV_LOG_FATAL, "-acodec copy and -vol are incompatible (frames are not decoded)");
            throw;
        }
        if ((par_dst->block_align == 1 || par_dst->block_align == 1152 || par_dst->block_align == 576) && par_dst->codec_id == AV_CODEC_ID_MP3)
            par_dst->block_align = 0;
        if (par_dst->codec_id == AV_CODEC_ID_AC3)
            par_dst->block_align = 0;
        break;
    case AVMEDIA_TYPE_VIDEO:
        if (ost->frame_aspect_ratio.num)
        { // overridden by the -aspect cli option
            sar =
                av_mul_q(ost->frame_aspect_ratio,
                         AVRational({par_dst->height, par_dst->width}));
            av_log(NULL, AV_LOG_WARNING, "Overriding aspect ratio "
                                         "with stream copy may produce invalid files\n");
        }
        else if (ist->st->sample_aspect_ratio.num)
            sar = ist->st->sample_aspect_ratio;
        else
            sar = par_src->sample_aspect_ratio;
        ost->st->sample_aspect_ratio = par_dst->sample_aspect_ratio = sar;
        ost->st->avg_frame_rate = ist->st->avg_frame_rate;
        ost->st->r_frame_rate = ist->st->r_frame_rate;
        break;
    }

    ost->mux_timebase = ist->st->time_base;

    return 0;
}

int init_output_bsfs(OutputStream *ost)
{
    AVBSFContext *ctx;
    int i, ret;

    if (!ost->nb_bitstream_filters)
        return 0;

    for (i = 0; i < ost->nb_bitstream_filters; i++)
    {
        ctx = ost->bsf_ctx[i];

        ret = avcodec_parameters_copy(ctx->par_in,
                                      i ? ost->bsf_ctx[i - 1]->par_out : ost->st->codecpar);
        if (ret < 0)
            return ret;

        ctx->time_base_in = i ? ost->bsf_ctx[i - 1]->time_base_out : ost->st->time_base;

        ret = av_bsf_init(ctx);
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Error initializing bitstream filter: %s\n",
                   ost->bsf_ctx[i]->filter->name);
            return ret;
        }
    }

    ctx = ost->bsf_ctx[ost->nb_bitstream_filters - 1];
    ret = avcodec_parameters_copy(ost->st->codecpar, ctx->par_out);
    if (ret < 0)
        return ret;

    ost->st->time_base = ctx->time_base_out;

    return 0;
}

void init_encoder_time_base(OutputStream *ost, AVRational default_time_base)
{
    InputStream *ist = get_input_stream(ost);
    AVCodecContext *enc_ctx = ost->enc_ctx;
    AVFormatContext *oc;

    if (ost->enc_timebase.num > 0)
    {
        enc_ctx->time_base = ost->enc_timebase;
        return;
    }

    if (ost->enc_timebase.num < 0)
    {
        if (ist)
        {
            enc_ctx->time_base = ist->st->time_base;
            return;
        }

        oc = output_files[ost->file_index]->ctx;
        av_log(oc, AV_LOG_WARNING, "Input stream data not available, using default time base\n");
    }

    enc_ctx->time_base = default_time_base;
}

void parse_forced_key_frames(char *kf, OutputStream *ost, AVCodecContext *avctx)
{
    char *p;
    int n = 1, i, size, index = 0;
    int64_t t, *pts;

    for (p = kf; *p; p++)
        if (*p == ',')
            n++;
    size = n;
    pts = (int64_t*)av_malloc_array(size, sizeof(*pts));
    if (!pts)
    {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate forced key frames array.");
        throw;
    }

    p = kf;
    for (i = 0; i < n; i++)
    {
        char *next = strchr(p, ',');

        if (next)
            *next++ = 0;

        if (!memcmp(p, "chapters", 8))
        {

            AVFormatContext *avf = output_files[ost->file_index]->ctx;
            int j;

            if (avf->nb_chapters > INT_MAX - size ||
                !(pts = (int64_t*)av_realloc_f(pts, size += avf->nb_chapters - 1,
                                     sizeof(*pts))))
            {
                av_log(NULL, AV_LOG_FATAL,
                       "Could not allocate forced key frames array.\n");
                throw;
            }
            t = p[8] ? parse_time_or_die("force_key_frames", p + 8, 1) : 0;
            t = av_rescale_q(t, AV_TIME_BASE_Q, avctx->time_base);

            for (j = 0; j < avf->nb_chapters; j++)
            {
                AVChapter *c = avf->chapters[j];
                av_assert1(index < size);
                pts[index++] = av_rescale_q(c->start, c->time_base,
                                            avctx->time_base) +
                               t;
            }
        }
        else
        {

            t = parse_time_or_die("force_key_frames", p, 1);
            av_assert1(index < size);
            pts[index++] = av_rescale_q(t, AV_TIME_BASE_Q, avctx->time_base);
        }

        p = next;
    }

    av_assert0(index == size);
    qsort(pts, size, sizeof(*pts), compare_int64);
    ost->forced_kf_count = size;
    ost->forced_kf_pts = pts;
}

void close_all_output_streams(OutputStream *ost, OSTFinished this_stream, OSTFinished others)
{
    int i;
    for (i = 0; i < nb_output_streams; i++)
    {
        OutputStream *ost2 = output_streams[i];
        ost2->finished = (OSTFinished)((int)ost2->finished | (int)(ost == ost2 ? this_stream : others));
    }
}

int check_recording_time(OutputStream *ost)
{
    OutputFile *of = output_files[ost->file_index];

    if (of->recording_time != INT64_MAX &&
        av_compare_ts(ost->sync_opts - ost->first_pts, ost->enc_ctx->time_base, of->recording_time,
                      AV_TIME_BASE_Q) >= 0)
    {
        close_output_stream(ost);
        return 0;
    }
    return 1;
}

int compare_int64(const void *a, const void *b)
{
    return FFDIFFSIGN(*(const int64_t *)a, *(const int64_t *)b);
}