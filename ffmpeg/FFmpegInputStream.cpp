#include "FFmpegInputStream.h"

extern "C"
{
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
}

#include "ffmpeg_utils.h"

FFmpegInputStream::FFmpegInputStream(AVFormatContext *s, int i, AVDictionary *opts) : st(s->streams[i]), dec_ctx(nullptr), fmt_ctx(s)
{
    AVCodec *codec;

    if (st->codecpar->codec_id == AV_CODEC_ID_PROBE)
    {
        av_log(NULL, AV_LOG_WARNING, "Failed to probe codec for input stream %d\n", st->index);
        return;
    }

    codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec)
    {
        av_log(NULL, AV_LOG_WARNING, "Unsupported codec with id %d for input stream %d\n", st->codecpar->codec_id, st->index);
        return;
    }

    AVDictionary *codec_opts = filter_codec_opts(opts, st->codecpar->codec_id, fmt_ctx, st, codec);
    AVDictionaryAutoCleanUp(codec_opts);

    dec_ctx = avcodec_alloc_context3(codec);
    if (!dec_ctx)
        throw;

    int err = avcodec_parameters_to_context(dec_ctx, st->codecpar);
    if (err < 0)
        throw;

    dec_ctx->pkt_timebase = st->time_base;
    dec_ctx->framerate = st->avg_frame_rate;

    if (avcodec_open2(dec_ctx, codec, &codec_opts) < 0)
    {
        av_log(NULL, AV_LOG_WARNING, "Could not open codec for input stream %d\n", st->index);
        return;
    }

    AVDictionaryEntry *t;
    while ((t = av_dict_get(codec_opts, "", NULL, AV_DICT_IGNORE_SUFFIX)))
    {
        av_log(NULL, AV_LOG_ERROR, "Option %s for input stream %d not found\n", t->key, st->index);
    }
}

void FFmpegInputStream::dumpToMatlab(mxArray *mxInfo, mwIndex index) const
{
#define BUF_SIZE 128
    char strbuf[BUF_SIZE];
    const char *s;
    const AVCodecDescriptor *cd;
    int ret = 0;
    const char *profile = NULL;

    mxArray *mxTMP;
    double *pr;

    ///////////////////////////////////////////
    // MACROs to set mxArray struct fields

#define mxSetScalarField(fname, fval) mxSetField(mxInfo, index, (fname), mxCreateDoubleScalar((fval)))
#define mxSetStringField(fname, fval) mxSetField(mxInfo, index, (fname), mxCreateString((fval)))
#define mxSetRatioField(fname, fval)            \
    mxTMP = mxCreateDoubleMatrix(1, 2, mxREAL); \
    pr = mxGetPr(mxTMP);                        \
    pr[0] = (fval).num;                         \
    pr[1] = (fval).den;                         \
    mxSetField(mxInfo, index, (fname), mxTMP)
#define mxSetColorRangeField(fname, fval) \
    s = av_color_range_name(fval);        \
    mxSetStringField(fname, s && (fval != AVCOL_RANGE_UNSPECIFIED) ? s : "unknown")
#define mxSetColorSpaceField(fname, fval) \
    s = av_color_space_name(fval);        \
    mxSetStringField(fname, s && (fval != AVCOL_SPC_UNSPECIFIED) ? s : "unknown")
#define mxSetColorPrimariesField(fname, fval) \
    s = av_color_primaries_name(fval);        \
    mxSetStringField(fname, s && (fval != AVCOL_PRI_UNSPECIFIED) ? s : "unknown")
#define mxSetColorTransferField(fname, fval) \
    s = av_color_transfer_name(fval);        \
    mxSetStringField(fname, s && (fval != AVCOL_TRC_UNSPECIFIED) ? s : "unknown")
#define mxSetChromaLocationField(fname, fval) \
    s = av_chroma_location_name(fval);        \
    mxSetStringField(fname, s && (fval != AVCHROMA_LOC_UNSPECIFIED) ? s : "unspecified")
#define mxSetTimestampField(fname, fval, is_duration)                           \
    if ((!is_duration && fval == AV_NOPTS_VALUE) || (is_duration && fval == 0)) \
        mxSetStringField(fname, "N/A");                                         \
    else                                                                        \
        mxSetScalarField(fname, (double)fval)
#define mxSetTimeField(fname, fval, is_duration)                                \
    if ((!is_duration && fval == AV_NOPTS_VALUE) || (is_duration && fval == 0)) \
        mxSetStringField(fname, "N/A");                                         \
    else                                                                        \
        mxSetScalarField(fname, fval *av_q2d(st->time_base))

    ///////////////////////////////////////////

    mxSetScalarField("index", st->index);

    AVCodecParameters *par = st->codecpar;
    if (cd = avcodec_descriptor_get(par->codec_id))
    {
        mxSetStringField("codec_name", cd->name);
        mxSetStringField("codec_long_name", cd->long_name ? cd->long_name : "unknown");
    }
    else
    {
        mxSetStringField("codec_name", "unknown");
        mxSetStringField("codec_long_name", "unknown");
    }

    if (profile = avcodec_profile_name(par->codec_id, par->profile))
    {
        mxSetStringField("profile", profile);
    }
    else
    {
        if (par->profile != FF_PROFILE_UNKNOWN)
        {
            char profile_num[12];
            snprintf(profile_num, sizeof(profile_num), "%d", par->profile);
            mxSetStringField("profile", profile_num);
        }
        else
        {
            mxSetStringField("profile", "unknown");
        }
    }

    s = av_get_media_type_string(par->codec_type);
    if (s)
        mxSetStringField("codec_type", s);
    else
        mxSetStringField("codec_type", "unknown");

    /* print AVI/FourCC tag */
    char fourcc[AV_FOURCC_MAX_STRING_SIZE];
    mxSetStringField("codec_tag_string", av_fourcc_make_string(fourcc, par->codec_tag));
    mxSetScalarField("codec_tag", par->codec_tag);

    switch (par->codec_type)
    {
    case AVMEDIA_TYPE_VIDEO:
        mxSetScalarField("width", par->width);
        mxSetScalarField("height", par->height);
        mxSetScalarField("has_b_frames", par->video_delay);
        AVRational sar;
        sar = av_guess_sample_aspect_ratio(fmt_ctx, st, NULL);
        if (sar.num)
        {
            mxSetRatioField("sample_aspect_ratio", sar);

            AVRational dar;
            av_reduce(&dar.num, &dar.den, par->width * sar.num,
                      par->height * sar.den, 1024 * 1024);
            mxSetRatioField("display_aspect_ratio", dar);
        }
        else
        {
            mxSetStringField("sample_aspect_ratio", "N/A");
            mxSetStringField("display_aspect_ratio", "N/A");
        }
        s = av_get_pix_fmt_name((AVPixelFormat)par->format);
        mxSetStringField("pix_fmt", s ? s : "unknown");

        mxSetScalarField("level", par->level);

        mxSetColorRangeField("color_range", par->color_range);
        mxSetColorSpaceField("color_space", par->color_space);
        mxSetColorPrimariesField("color_primaries", par->color_primaries);
        mxSetColorTransferField("color_transfer", par->color_trc);
        mxSetChromaLocationField("chroma_location", par->chroma_location);

        if (par->field_order == AV_FIELD_PROGRESSIVE)
            mxSetStringField("field_order", "progressive");
        else if (par->field_order == AV_FIELD_TT)
            mxSetStringField("field_order", "tt");
        else if (par->field_order == AV_FIELD_BB)
            mxSetStringField("field_order", "bb");
        else if (par->field_order == AV_FIELD_TB)
            mxSetStringField("field_order", "tb");
        else if (par->field_order == AV_FIELD_BT)
            mxSetStringField("field_order", "bt");
        else
            mxSetStringField("field_order", "unknown");

        if (dec_ctx)
            mxSetScalarField("refs", dec_ctx->refs);
        break;

    case AVMEDIA_TYPE_AUDIO:
        s = av_get_sample_fmt_name((AVSampleFormat)par->format);
        mxSetStringField("sample_fmt", s ? s : "unknown");
        mxSetScalarField("sample_rate", par->sample_rate);
        mxSetScalarField("channels", par->channels);

        if (par->channel_layout)
        {
            av_get_channel_layout_string(strbuf, BUF_SIZE, par->channels, par->channel_layout);
            mxSetStringField("channel_layout", strbuf);
        }
        else
        {
            mxSetStringField("channel_layout", "unknown");
        }

        mxSetScalarField("bits_per_sample", av_get_bits_per_sample(par->codec_id));
        break;

    case AVMEDIA_TYPE_SUBTITLE:
        if (par->width)
            mxSetScalarField("width", par->width);
        else
            mxSetStringField("width", "N/A");
        if (par->height)
            mxSetScalarField("height", par->height);
        else
            mxSetStringField("height", "N/A");
        break;
    }

    if (fmt_ctx->iformat->flags)
        mxSetScalarField("id", st->id);
    else
        mxSetStringField("id", "N/A");
    mxSetRatioField("r_frame_rate", st->r_frame_rate);
    mxSetRatioField("avg_frame_rate", st->avg_frame_rate);
    mxSetRatioField("time_base", st->time_base);
    mxSetTimestampField("start_pts", st->start_time, false);
    mxSetTimeField("start_time", st->start_time, false);
    mxSetTimestampField("duration_ts", st->duration, true);
    mxSetTimeField("duration", st->duration, true);
    if (par->bit_rate > 0)
        mxSetScalarField("bit_rate", (double)par->bit_rate);
    else
        mxSetStringField("bit_rate", "N/A");
    if (dec_ctx && dec_ctx->bits_per_raw_sample > 0)
        mxSetScalarField("bits_per_raw_sample", dec_ctx->bits_per_raw_sample);
    else
        mxSetStringField("bits_per_raw_sample", "N/A");
    if (st->nb_frames)
        mxSetScalarField("nb_frames", (double)st->nb_frames);
    else
        mxSetStringField("nb_frames", "N/A");

    /* Get disposition information */
    int dispocount = (st->disposition & AV_DISPOSITION_DEFAULT) ? 1 : 0;

#define COUNT_DISPOSITION(flagname)                  \
    if (st->disposition & AV_DISPOSITION_##flagname) \
    ++dispocount

    COUNT_DISPOSITION(DUB);
    COUNT_DISPOSITION(ORIGINAL);
    COUNT_DISPOSITION(COMMENT);
    COUNT_DISPOSITION(LYRICS);
    COUNT_DISPOSITION(KARAOKE);
    COUNT_DISPOSITION(FORCED);
    COUNT_DISPOSITION(HEARING_IMPAIRED);
    COUNT_DISPOSITION(VISUAL_IMPAIRED);
    COUNT_DISPOSITION(CLEAN_EFFECTS);
    COUNT_DISPOSITION(ATTACHED_PIC);
    COUNT_DISPOSITION(TIMED_THUMBNAILS);
    COUNT_DISPOSITION(CAPTIONS);
    COUNT_DISPOSITION(DESCRIPTIONS);
    COUNT_DISPOSITION(METADATA);
    COUNT_DISPOSITION(DEPENDENT);
    COUNT_DISPOSITION(STILL_IMAGE);

    mxArray *mxCell = mxCreateCellMatrix(1, dispocount);
    mxSetField(mxTMP, index, "disposition", mxCell);

    int i = 0;
#define mxSetDispositionField(flagname, name)        \
    if (st->disposition & AV_DISPOSITION_##flagname) \
    mxSetCell(mxCell, i++, mxCreateString(name))

    mxSetDispositionField(DEFAULT, "default");
    mxSetDispositionField(DUB, "dub");
    mxSetDispositionField(ORIGINAL, "original");
    mxSetDispositionField(COMMENT, "comment");
    mxSetDispositionField(LYRICS, "lyrics");
    mxSetDispositionField(KARAOKE, "karaoke");
    mxSetDispositionField(FORCED, "forced");
    mxSetDispositionField(HEARING_IMPAIRED, "hearing_impaired");
    mxSetDispositionField(VISUAL_IMPAIRED, "visual_impaired");
    mxSetDispositionField(CLEAN_EFFECTS, "clean_effects");
    mxSetDispositionField(ATTACHED_PIC, "attached_pic");
    mxSetDispositionField(TIMED_THUMBNAILS, "timed_thumbnails");
    mxSetDispositionField(CAPTIONS, "captions");
    mxSetDispositionField(DESCRIPTIONS, "descriptions");
    mxSetDispositionField(METADATA, "metadata");
    mxSetDispositionField(DEPENDENT, "dependent");
    mxSetDispositionField(STILL_IMAGE, "still_image");

    mxSetField(mxInfo, index, "metadata", mxCreateTags(st->metadata));
}

#define ARRAY_LENGTH(_array_) (sizeof(_array_) / sizeof(_array_[0]))

mxArray *FFmpegInputStream::createMxInfoStruct(mwSize size)
{
    return mxCreateStructMatrix(size, 1, ARRAY_LENGTH(field_names), field_names);
}

const char *FFmpegInputStream::field_names[] = {
    "index",
    "codec_name",
    "codec_long_name",
    "profile",
    "codec_type",
    "codec_tag_string",
    "codec_tag",
    "width", // video
    "height",
    "has_b_frames",
    "sample_aspect_ratio",
    "display_aspect_ratio",
    "pix_fmt",
    "level",
    "color_range",
    "color_space",
    "color_transfer",
    "color_primaries",
    "chroma_location",
    "field_order",
    "refs",
    "sample_fmt", // audio
    "sample_rate",
    "channel_layout",
    "bits_per_sample", // end
    "id",
    "r_frame_rate",
    "avg_frame_rate",
    "time_base",
    "start_pts",
    "start_time",
    "duration_ts",
    "duration",
    "bit_rate",
    "bits_per_raw_sample",
    "nb_frames",
    "metadata"};
