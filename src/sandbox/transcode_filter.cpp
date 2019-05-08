#include "transcode_filter.h"

#include <string>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/avstring.h>
#include <libswscale/swscale.h>
#include <libavutil/display.h>
}

#include "avexception.h"
#include "transcode_inputfile.h"
#include "transcode_inputstream.h"
#include "transcode_outputfile.h"
#include "transcode_outputstream.h"
#include "transcode_hw.h"
#include "transcode_utils.h"

std::vector<FilterGraph *> filtergraphs;
InputStream **input_streams = NULL;
int nb_input_streams = 0;
InputFile **input_files = NULL;
int nb_input_files = 0;

OutputStream **output_streams = NULL;
int nb_output_streams = 0;
OutputFile **output_files = NULL;
int nb_output_files = 0;
int audio_volume = 256;
int do_deinterlace = 0;
int copy_ts = 0;
int start_at_zero = 0;
int audio_sync_method = 0;
float audio_drift_threshold = 0.1;
int filter_nbthreads = 0;
int filter_complex_nbthreads = 0;

AVBufferRef *hw_device_ctx;
HWDevice *filter_hw_device;

const enum AVPixelFormat *get_compliance_unofficial_pix_fmts(enum AVCodecID codec_id, const enum AVPixelFormat default_formats[])
{
    static const enum AVPixelFormat mjpeg_formats[] =
        {AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
         AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
         AV_PIX_FMT_NONE};
    static const enum AVPixelFormat ljpeg_formats[] =
        {AV_PIX_FMT_BGR24, AV_PIX_FMT_BGRA, AV_PIX_FMT_BGR0,
         AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P,
         AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P,
         AV_PIX_FMT_NONE};

    if (codec_id == AV_CODEC_ID_MJPEG)
    {
        return mjpeg_formats;
    }
    else if (codec_id == AV_CODEC_ID_LJPEG)
    {
        return ljpeg_formats;
    }
    else
    {
        return default_formats;
    }
}

enum AVPixelFormat choose_pixel_fmt(AVStream *st, AVCodecContext *enc_ctx, AVCodec *codec, enum AVPixelFormat target)
{
    if (codec && codec->pix_fmts)
    {
        const enum AVPixelFormat *p = codec->pix_fmts;
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(target);
        //FIXME: This should check for AV_PIX_FMT_FLAG_ALPHA after PAL8 pixel format without alpha is implemented
        int has_alpha = desc ? desc->nb_components % 2 == 0 : 0;
        enum AVPixelFormat best = AV_PIX_FMT_NONE;

        if (enc_ctx->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL)
        {
            p = get_compliance_unofficial_pix_fmts(enc_ctx->codec_id, p);
        }
        for (; *p != AV_PIX_FMT_NONE; p++)
        {
            best = avcodec_find_best_pix_fmt_of_2(best, *p, target, has_alpha, NULL);
            if (*p == target)
                break;
        }
        if (*p == AV_PIX_FMT_NONE)
        {
            if (target != AV_PIX_FMT_NONE)
                av_log(NULL, AV_LOG_WARNING,
                       "Incompatible pixel format '%s' for codec '%s', auto-selecting format '%s'\n",
                       av_get_pix_fmt_name(target),
                       codec->name,
                       av_get_pix_fmt_name(best));
            return best;
        }
    }
    return target;
}

void choose_sample_fmt(AVStream *st, AVCodec *codec)
{
    if (codec && codec->sample_fmts)
    {
        const enum AVSampleFormat *p = codec->sample_fmts;
        for (; *p != -1; p++)
        {
            if (*p == st->codecpar->format)
                break;
        }
        if (*p == -1)
        {
            if ((codec->capabilities & AV_CODEC_CAP_LOSSLESS) && av_get_sample_fmt_name((AVSampleFormat)st->codecpar->format) > av_get_sample_fmt_name(codec->sample_fmts[0]))
                av_log(NULL, AV_LOG_ERROR, "Conversion will not be lossless.\n");
            if (av_get_sample_fmt_name((AVSampleFormat)st->codecpar->format))
                av_log(NULL, AV_LOG_WARNING,
                       "Incompatible sample format '%s' for codec '%s', auto-selecting format '%s'\n",
                       av_get_sample_fmt_name((AVSampleFormat)st->codecpar->format),
                       codec->name,
                       av_get_sample_fmt_name(codec->sample_fmts[0]));
            st->codecpar->format = codec->sample_fmts[0];
        }
    }
}

static char *choose_pix_fmts(OutputFilter *ofilter)
{
    OutputStream *ost = ofilter->ost;
    AVDictionaryEntry *strict_dict = av_dict_get(ost->encoder_opts, "strict", NULL, 0);
    if (strict_dict)
        // used by choose_pixel_fmt() and below
        av_opt_set(ost->enc_ctx, "strict", strict_dict->value, 0);

    if (ost->keep_pix_fmt)
    {
        avfilter_graph_set_auto_convert(ofilter->graph->graph,
                                        AVFILTER_AUTO_CONVERT_NONE);
        if (ost->enc_ctx->pix_fmt == AV_PIX_FMT_NONE)
            return NULL;
        return av_strdup(av_get_pix_fmt_name(ost->enc_ctx->pix_fmt));
    }
    if (ost->enc_ctx->pix_fmt != AV_PIX_FMT_NONE)
    {
        return av_strdup(av_get_pix_fmt_name(choose_pixel_fmt(ost->st, ost->enc_ctx, ost->enc, ost->enc_ctx->pix_fmt)));
    }
    else if (ost->enc && ost->enc->pix_fmts)
    {
        const enum AVPixelFormat *p;
        AVIOContext *s = NULL;
        uint8_t *ret;
        int len;

        if (avio_open_dyn_buf(&s) < 0)
        {
            AVException::log(AV_LOG_FATAL, "Failed to open dynamic buffer (avio_open_dyn_buf())");
            throw;
        }

        p = ost->enc->pix_fmts;
        if (ost->enc_ctx->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL)
        {
            p = get_compliance_unofficial_pix_fmts(ost->enc_ctx->codec_id, p);
        }

        for (; *p != AV_PIX_FMT_NONE; p++)
        {
            const char *name = av_get_pix_fmt_name(*p);
            avio_printf(s, "%s|", name);
        }
        len = avio_close_dyn_buf(s, &ret);
        ret[len - 1] = 0;
        return (char *)ret;
    }
    else
        return NULL;
}

/* Define a function for building a string containing a list of
 * allowed formats. */
#define DEF_CHOOSE_FORMAT(suffix, type, var, supported_list, none, get_name)      \
    static char *choose_##suffix(OutputFilter *ofilter)                           \
    {                                                                             \
        if (ofilter->var != none)                                                 \
        {                                                                         \
            get_name(ofilter->var);                                               \
            return av_strdup(name);                                               \
        }                                                                         \
        else if (ofilter->supported_list)                                         \
        {                                                                         \
            const type *p;                                                        \
            AVIOContext *s = NULL;                                                \
            uint8_t *ret;                                                         \
            int len;                                                              \
                                                                                  \
            if (avio_open_dyn_buf(&s) < 0)                                        \
            {                                                                     \
                AVException::log(AV_LOG_FATAL, "Failed to open dynamic buffer."); \
                throw;                                                            \
            }                                                                     \
                                                                                  \
            for (p = (const type *)ofilter->supported_list; *p != none; p++)                    \
            {                                                                     \
                get_name(*p);                                                     \
                avio_printf(s, "%s|", name);                                      \
            }                                                                     \
            len = avio_close_dyn_buf(s, &ret);                                    \
            ret[len - 1] = 0;                                                     \
            return (char *)ret;                                                   \
        }                                                                         \
        else                                                                      \
            return NULL;                                                          \
    }

//DEF_CHOOSE_FORMAT(pix_fmts, enum AVPixelFormat, format, formats, AV_PIX_FMT_NONE,
//                  GET_PIX_FMT_NAME)

#define GET_SAMPLE_FMT_NAME(sample_fmt) \
    const char *name = av_get_sample_fmt_name((AVSampleFormat)sample_fmt)

DEF_CHOOSE_FORMAT(sample_fmts, AVSampleFormat, format, formats,
                  AV_SAMPLE_FMT_NONE, GET_SAMPLE_FMT_NAME)

#define GET_SAMPLE_RATE_NAME(rate) \
    char name[16];                 \
    snprintf(name, sizeof(name), "%d", rate);

DEF_CHOOSE_FORMAT(sample_rates, int, sample_rate, sample_rates, 0,
                  GET_SAMPLE_RATE_NAME)

#define GET_CH_LAYOUT_NAME(ch_layout) \
    char name[16];                    \
    snprintf(name, sizeof(name), "0x%" PRIx64, ch_layout);

DEF_CHOOSE_FORMAT(channel_layouts, uint64_t, channel_layout, channel_layouts, 0,
                  GET_CH_LAYOUT_NAME)

int init_simple_filtergraph(InputStream *ist, OutputStream *ost)
{
    FilterGraph *fg = (FilterGraph *)av_mallocz(sizeof(*fg));

    if (!fg)
    {
        AVException::log(AV_LOG_FATAL, "Failed to allocate memory for FilterGraph object.");
        throw;
    }
    fg->index = filtergraphs.size();

    fg->outputs.insert(fg->outputs.begin(), (OutputFilter *)av_mallocz(sizeof(*fg->outputs[0])));
    fg->outputs[0]->ost = ost;
    fg->outputs[0]->graph = fg;
    fg->outputs[0]->format = -1;

    ost->filter = fg->outputs[0];

    fg->inputs.insert(fg->inputs.begin(), (InputFilter *)av_mallocz(sizeof(*fg->inputs[0])));
    fg->inputs[0]->ist = ist;
    fg->inputs[0]->graph = fg;
    fg->inputs[0]->format = -1;

    fg->inputs[0]->frame_queue = av_fifo_alloc(8 * sizeof(AVFrame *));
    if (!fg->inputs[0]->frame_queue)
    {
        AVException::log(AV_LOG_FATAL, "Failed to allocate frame FIFO queue.");
        throw;
    }

    ist->filters.push_back(fg->inputs[0]);

    filtergraphs.push_back(fg);

    return 0;
}

static char *describe_filter_link(FilterGraph *fg, AVFilterInOut *inout, int in)
{
    AVFilterContext *ctx = inout->filter_ctx;
    AVFilterPad *pads = in ? ctx->input_pads : ctx->output_pads;
    int nb_pads = in ? ctx->nb_inputs : ctx->nb_outputs;
    AVIOContext *pb;
    uint8_t *res = NULL;

    if (avio_open_dyn_buf(&pb) < 0)
        throw;

    avio_printf(pb, "%s", ctx->filter->name);
    if (nb_pads > 1)
        avio_printf(pb, ":%s", avfilter_pad_get_name(pads, inout->pad_idx));
    avio_w8(pb, 0);
    avio_close_dyn_buf(pb, &res);
    return (char *)res;
}

int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec)
{
    int ret = avformat_match_stream_specifier(s, st, spec);
    if (ret < 0)
        av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
    return ret;
}

static void init_input_filter(FilterGraph *fg, AVFilterInOut *in)
{
    InputStream *ist = NULL;
    enum AVMediaType type = avfilter_pad_get_type(in->filter_ctx->input_pads, in->pad_idx);
    int i;

    // TODO: support other filter types
    if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO)
    {
        av_log(NULL, AV_LOG_FATAL, "Only video and audio filters supported "
                                   "currently.\n");
        throw;
    }

    if (in->name)
    {
        AVFormatContext *s;
        AVStream *st = NULL;
        char *p;
        int file_idx = strtol(in->name, &p, 0);

        if (file_idx < 0 || file_idx >= nb_input_files)
        {
            av_log(NULL, AV_LOG_FATAL, "Invalid file index %d in filtergraph description %s.\n",
                   file_idx, fg->graph_desc);
            throw;
        }
        s = input_files[file_idx]->ctx;

        for (i = 0; i < s->nb_streams; i++)
        {
            enum AVMediaType stream_type = s->streams[i]->codecpar->codec_type;
            if (stream_type != type &&
                !(stream_type == AVMEDIA_TYPE_SUBTITLE &&
                  type == AVMEDIA_TYPE_VIDEO /* sub2video hack */))
                continue;
            if (check_stream_specifier(s, s->streams[i], *p == ':' ? p + 1 : p) == 1)
            {
                st = s->streams[i];
                break;
            }
        }
        if (!st)
        {
            av_log(NULL, AV_LOG_FATAL, "Stream specifier '%s' in filtergraph description %s "
                                       "matches no streams.\n",
                   p, fg->graph_desc);
            throw;
        }
        ist = input_streams[input_files[file_idx]->ist_index + st->index];
    }
    else
    {
        /* find the first unused stream of corresponding type */
        for (i = 0; i < nb_input_streams; i++)
        {
            ist = input_streams[i];
            if (ist->dec_ctx->codec_type == type && ist->discard)
                break;
        }
        if (i == nb_input_streams)
        {
            av_log(NULL, AV_LOG_FATAL, "Cannot find a matching stream for "
                                       "unlabeled input pad %d on filter %s\n",
                   in->pad_idx,
                   in->filter_ctx->name);
            throw;
        }
    }
    av_assert0(ist);

    ist->discard = 0;
    ist->decoding_needed |= DECODING_FOR_FILTER;
    ist->st->discard = AVDISCARD_NONE;

    InputFilter *newinfilt = (InputFilter *)av_mallocz(sizeof(InputFilter));
    fg->inputs.push_back(newinfilt);
    newinfilt->ist = ist;
    newinfilt->graph = fg;
    newinfilt->format = -1;
    newinfilt->type = ist->st->codecpar->codec_type;
    newinfilt->name = (uint8_t *)describe_filter_link(fg, in, 1);

    newinfilt->frame_queue = av_fifo_alloc(8 * sizeof(AVFrame *));
    if (!newinfilt->frame_queue)
        throw;

    ist->filters.push_back(newinfilt);
}

int init_complex_filtergraph(FilterGraph *fg)
{
    AVFilterInOut *inputs, *outputs, *cur;
    AVFilterGraph *graph;
    int ret = 0;

    /* this graph is only used for determining the kinds of inputs
     * and outputs we have, and is discarded on exit from this function */
    graph = avfilter_graph_alloc();
    if (!graph)
        return AVERROR(ENOMEM);
    graph->nb_threads = 1;

    ret = avfilter_graph_parse2(graph, fg->graph_desc, &inputs, &outputs);
    if (ret < 0)
        goto fail;

    for (cur = inputs; cur; cur = cur->next)
        init_input_filter(fg, cur);

    for (cur = outputs; cur;)
    {
        OutputFilter *newoutfilt = (OutputFilter *)av_mallocz(sizeof(OutputFilter));
        if (!newoutfilt)
            throw;

        fg->outputs.push_back(newoutfilt);

        newoutfilt->graph = fg;
        newoutfilt->out_tmp = cur;
        newoutfilt->type = avfilter_pad_get_type(cur->filter_ctx->output_pads,
                                                 cur->pad_idx);
        newoutfilt->name = (uint8_t *)describe_filter_link(fg, cur, 0);
        cur = cur->next;
        newoutfilt->out_tmp->next = NULL;
    }

fail:
    avfilter_inout_free(&inputs);
    avfilter_graph_free(&graph);
    return ret;
}

static int insert_trim(int64_t start_time, int64_t duration,
                       AVFilterContext **last_filter, int *pad_idx,
                       const char *filter_name)
{
    AVFilterGraph *graph = (*last_filter)->graph;
    AVFilterContext *ctx;
    const AVFilter *trim;
    enum AVMediaType type = avfilter_pad_get_type((*last_filter)->output_pads, *pad_idx);
    const char *name = (type == AVMEDIA_TYPE_VIDEO) ? "trim" : "atrim";
    int ret = 0;

    if (duration == INT64_MAX && start_time == AV_NOPTS_VALUE)
        return 0;

    trim = avfilter_get_by_name(name);
    if (!trim)
    {
        av_log(NULL, AV_LOG_ERROR, "%s filter not present, cannot limit "
                                   "recording time.\n",
               name);
        return AVERROR_FILTER_NOT_FOUND;
    }

    ctx = avfilter_graph_alloc_filter(graph, trim, filter_name);
    if (!ctx)
        return AVERROR(ENOMEM);

    if (duration != INT64_MAX)
    {
        ret = av_opt_set_int(ctx, "durationi", duration,
                             AV_OPT_SEARCH_CHILDREN);
    }
    if (ret >= 0 && start_time != AV_NOPTS_VALUE)
    {
        ret = av_opt_set_int(ctx, "starti", start_time,
                             AV_OPT_SEARCH_CHILDREN);
    }
    if (ret < 0)
    {
        av_log(ctx, AV_LOG_ERROR, "Error configuring the %s filter", name);
        return ret;
    }

    ret = avfilter_init_str(ctx, NULL);
    if (ret < 0)
        return ret;

    ret = avfilter_link(*last_filter, *pad_idx, ctx, 0);
    if (ret < 0)
        return ret;

    *last_filter = ctx;
    *pad_idx = 0;
    return 0;
}

static int insert_filter(AVFilterContext **last_filter, int *pad_idx,
                         const char *filter_name, const char *args)
{
    AVFilterGraph *graph = (*last_filter)->graph;
    AVFilterContext *ctx;
    int ret;

    ret = avfilter_graph_create_filter(&ctx,
                                       avfilter_get_by_name(filter_name),
                                       filter_name, args, NULL, graph);
    if (ret < 0)
        return ret;

    ret = avfilter_link(*last_filter, *pad_idx, ctx, 0);
    if (ret < 0)
        return ret;

    *last_filter = ctx;
    *pad_idx = 0;
    return 0;
}

static int configure_output_video_filter(FilterGraph *fg, OutputFilter *ofilter, AVFilterInOut *out)
{
    char *pix_fmts;
    OutputStream *ost = ofilter->ost;
    OutputFile *of = output_files[ost->file_index];
    AVFilterContext *last_filter = out->filter_ctx;
    int pad_idx = out->pad_idx;
    int ret;
    char name[255];

    snprintf(name, sizeof(name), "out_%d_%d", ost->file_index, ost->index);
    ret = avfilter_graph_create_filter(&ofilter->filter,
                                       avfilter_get_by_name("buffersink"),
                                       name, NULL, NULL, fg->graph);

    if (ret < 0)
        return ret;

    if (ofilter->width || ofilter->height)
    {
        char args[255];
        AVFilterContext *filter;
        AVDictionaryEntry *e = NULL;

        snprintf(args, sizeof(args), "%d:%d",
                 ofilter->width, ofilter->height);

        while ((e = av_dict_get(ost->sws_dict, "", e,
                                AV_DICT_IGNORE_SUFFIX)))
        {
            av_strlcatf(args, sizeof(args), ":%s=%s", e->key, e->value);
        }

        snprintf(name, sizeof(name), "scaler_out_%d_%d",
                 ost->file_index, ost->index);
        if ((ret = avfilter_graph_create_filter(&filter, avfilter_get_by_name("scale"),
                                                name, args, NULL, fg->graph)) < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, pad_idx, filter, 0)) < 0)
            return ret;

        last_filter = filter;
        pad_idx = 0;
    }

    if ((pix_fmts = choose_pix_fmts(ofilter)))
    {
        AVFilterContext *filter;
        snprintf(name, sizeof(name), "format_out_%d_%d",
                 ost->file_index, ost->index);
        ret = avfilter_graph_create_filter(&filter,
                                           avfilter_get_by_name("format"),
                                           "format", pix_fmts, NULL, fg->graph);
        av_freep(&pix_fmts);
        if (ret < 0)
            return ret;
        if ((ret = avfilter_link(last_filter, pad_idx, filter, 0)) < 0)
            return ret;

        last_filter = filter;
        pad_idx = 0;
    }

    if (ost->frame_rate.num && 0)
    {
        AVFilterContext *fps;
        char args[255];

        snprintf(args, sizeof(args), "fps=%d/%d", ost->frame_rate.num,
                 ost->frame_rate.den);
        snprintf(name, sizeof(name), "fps_out_%d_%d",
                 ost->file_index, ost->index);
        ret = avfilter_graph_create_filter(&fps, avfilter_get_by_name("fps"),
                                           name, args, NULL, fg->graph);
        if (ret < 0)
            return ret;

        ret = avfilter_link(last_filter, pad_idx, fps, 0);
        if (ret < 0)
            return ret;
        last_filter = fps;
        pad_idx = 0;
    }

    snprintf(name, sizeof(name), "trim_out_%d_%d",
             ost->file_index, ost->index);
    ret = insert_trim(of->start_time, of->recording_time,
                      &last_filter, &pad_idx, name);
    if (ret < 0)
        return ret;

    if ((ret = avfilter_link(last_filter, pad_idx, ofilter->filter, 0)) < 0)
        return ret;

    return 0;
}

static int configure_output_audio_filter(FilterGraph *fg, OutputFilter *ofilter, AVFilterInOut *out)
{
    OutputStream *ost = ofilter->ost;
    OutputFile *of = output_files[ost->file_index];
    AVCodecContext *codec = ost->enc_ctx;
    AVFilterContext *last_filter = out->filter_ctx;
    int pad_idx = out->pad_idx;
    char *sample_fmts, *sample_rates, *channel_layouts;
    char name[255];
    int ret;

    snprintf(name, sizeof(name), "out_%d_%d", ost->file_index, ost->index);
    ret = avfilter_graph_create_filter(&ofilter->filter,
                                       avfilter_get_by_name("abuffersink"),
                                       name, NULL, NULL, fg->graph);
    if (ret < 0)
        return ret;
    if ((ret = av_opt_set_int(ofilter->filter, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
        return ret;

#define AUTO_INSERT_FILTER(opt_name, filter_name, arg)                               \
    do                                                                               \
    {                                                                                \
        AVFilterContext *filt_ctx;                                                   \
                                                                                     \
        av_log(NULL, AV_LOG_INFO, opt_name " is forwarded to lavfi "                 \
                                           "similarly to -af " filter_name "=%s.\n", \
               arg);                                                                 \
                                                                                     \
        ret = avfilter_graph_create_filter(&filt_ctx,                                \
                                           avfilter_get_by_name(filter_name),        \
                                           filter_name, arg, NULL, fg->graph);       \
        if (ret < 0)                                                                 \
            return ret;                                                              \
                                                                                     \
        ret = avfilter_link(last_filter, pad_idx, filt_ctx, 0);                      \
        if (ret < 0)                                                                 \
            return ret;                                                              \
                                                                                     \
        last_filter = filt_ctx;                                                      \
        pad_idx = 0;                                                                 \
    } while (0)
    if (ost->audio_channels_mapped)
    {
        int i;
        std::string pan_buf = "0x";
        // AVBPrint pan_buf;
        // av_bprint_init(&pan_buf, 256, 8192);
        pan_buf += std::to_string(av_get_default_channel_layout(ost->audio_channels_mapped));
        // av_bprintf(&pan_buf, "0x%" PRIx64,
        //            av_get_default_channel_layout(ost->audio_channels_mapped));
        for (i = 0; i < ost->audio_channels_mapped; i++)
            if (ost->audio_channels_map[i] != -1)
                pan_buf += "|c" + std::to_string(i) + "=c" + std::to_string(ost->audio_channels_map[i]);
        // av_bprintf(&pan_buf, "|c%d=c%d", i, ost->audio_channels_map[i]);

        AUTO_INSERT_FILTER("-map_channel", "pan", pan_buf.c_str());
        // AUTO_INSERT_FILTER("-map_channel", "pan", pan_buf.str);
        // av_bprint_finalize(&pan_buf, NULL);
    }

    if (codec->channels && !codec->channel_layout)
        codec->channel_layout = av_get_default_channel_layout(codec->channels);

    sample_fmts = choose_sample_fmts(ofilter);
    sample_rates = choose_sample_rates(ofilter);
    channel_layouts = choose_channel_layouts(ofilter);
    if (sample_fmts || sample_rates || channel_layouts)
    {
        AVFilterContext *format;
        char args[256];
        args[0] = 0;

        if (sample_fmts)
            av_strlcatf(args, sizeof(args), "sample_fmts=%s:",
                        sample_fmts);
        if (sample_rates)
            av_strlcatf(args, sizeof(args), "sample_rates=%s:",
                        sample_rates);
        if (channel_layouts)
            av_strlcatf(args, sizeof(args), "channel_layouts=%s:",
                        channel_layouts);

        av_freep(&sample_fmts);
        av_freep(&sample_rates);
        av_freep(&channel_layouts);

        snprintf(name, sizeof(name), "format_out_%d_%d",
                 ost->file_index, ost->index);
        ret = avfilter_graph_create_filter(&format,
                                           avfilter_get_by_name("aformat"),
                                           name, args, NULL, fg->graph);
        if (ret < 0)
            return ret;

        ret = avfilter_link(last_filter, pad_idx, format, 0);
        if (ret < 0)
            return ret;

        last_filter = format;
        pad_idx = 0;
    }

    if (audio_volume != 256 && 0)
    {
        char args[256];

        snprintf(args, sizeof(args), "%f", audio_volume / 256.);
        AUTO_INSERT_FILTER("-vol", "volume", args);
    }

    if (ost->apad && of->shortest)
    {
        char args[256];
        int i;

        for (i = 0; i < of->ctx->nb_streams; i++)
            if (of->ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                break;

        if (i < of->ctx->nb_streams)
        {
            snprintf(args, sizeof(args), "%s", ost->apad);
            AUTO_INSERT_FILTER("-apad", "apad", args);
        }
    }

    snprintf(name, sizeof(name), "trim for output stream %d:%d",
             ost->file_index, ost->index);
    ret = insert_trim(of->start_time, of->recording_time,
                      &last_filter, &pad_idx, name);
    if (ret < 0)
        return ret;

    if ((ret = avfilter_link(last_filter, pad_idx, ofilter->filter, 0)) < 0)
        return ret;

    return 0;
}

int configure_output_filter(FilterGraph *fg, OutputFilter *ofilter, AVFilterInOut *out)
{
    if (!ofilter->ost)
    {
        av_log(NULL, AV_LOG_FATAL, "Filter %s has an unconnected output\n", ofilter->name);
        throw;
    }

    switch (avfilter_pad_get_type(out->filter_ctx->output_pads, out->pad_idx))
    {
    case AVMEDIA_TYPE_VIDEO:
        return configure_output_video_filter(fg, ofilter, out);
    case AVMEDIA_TYPE_AUDIO:
        return configure_output_audio_filter(fg, ofilter, out);
    default:
        av_assert0(0);
    }
}

void check_filter_outputs(void)
{
    int i;
    for (i = 0; i < filtergraphs.size(); i++)
    {
        int n;
        for (n = 0; n < filtergraphs[i]->nb_outputs; n++)
        {
            OutputFilter *output = filtergraphs[i]->outputs[n];
            if (!output->ost)
            {
                av_log(NULL, AV_LOG_FATAL, "Filter %s has an unconnected output\n", output->name);
                throw;
            }
        }
    }
}

static int sub2video_prepare(InputStream *ist, InputFilter *ifilter)
{
    AVFormatContext *avf = input_files[ist->file_index]->ctx;
    int i, w, h;

    /* Compute the size of the canvas for the subtitles stream.
       If the subtitles codecpar has set a size, use it. Otherwise use the
       maximum dimensions of the video streams in the same file. */
    w = ifilter->width;
    h = ifilter->height;
    if (!(w && h))
    {
        for (i = 0; i < avf->nb_streams; i++)
        {
            if (avf->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                w = FFMAX(w, avf->streams[i]->codecpar->width);
                h = FFMAX(h, avf->streams[i]->codecpar->height);
            }
        }
        if (!(w && h))
        {
            w = FFMAX(w, 720);
            h = FFMAX(h, 576);
        }
        av_log(avf, AV_LOG_INFO, "sub2video: using %dx%d canvas\n", w, h);
    }
    ist->sub2video.w = ifilter->width = w;
    ist->sub2video.h = ifilter->height = h;

    ifilter->width = ist->dec_ctx->width ? ist->dec_ctx->width : ist->sub2video.w;
    ifilter->height = ist->dec_ctx->height ? ist->dec_ctx->height : ist->sub2video.h;

    /* rectangles are AV_PIX_FMT_PAL8, but we have no guarantee that the
       palettes for all rectangles are identical or compatible */
    ifilter->format = AV_PIX_FMT_RGB32;

    ist->sub2video.frame = av_frame_alloc();
    if (!ist->sub2video.frame)
        return AVERROR(ENOMEM);
    ist->sub2video.last_pts = INT64_MIN;
    return 0;
}

double get_rotation(AVStream *st);

static int configure_input_video_filter(FilterGraph *fg, InputFilter *ifilter,
                                        AVFilterInOut *in)
{
    AVFilterContext *last_filter;
    const AVFilter *buffer_filt = avfilter_get_by_name("buffer");
    InputStream *ist = ifilter->ist;
    InputFile *f = input_files[ist->file_index];
    AVRational tb = ist->framerate.num ? av_inv_q(ist->framerate) : ist->st->time_base;
    AVRational fr = ist->framerate;
    AVRational sar;
    std::string args;
    char name[255];
    int ret, pad_idx = 0;
    int64_t tsoffset = 0;
    AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();

    if (!par)
        return AVERROR(ENOMEM);
    memset(par, 0, sizeof(*par));
    par->format = AV_PIX_FMT_NONE;

    if (ist->dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot connect video filter to audio input\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if (!fr.num)
        fr = av_guess_frame_rate(input_files[ist->file_index]->ctx, ist->st, NULL);

    if (ist->dec_ctx->codec_type == AVMEDIA_TYPE_SUBTITLE)
    {
        ret = sub2video_prepare(ist, ifilter);
        if (ret < 0)
            goto fail;
    }

    sar = ifilter->sample_aspect_ratio;
    if (!sar.den)
        sar = AVRational({0, 1});
    args = "video_size=";
    args += std::to_string(ifilter->width) + "x" + std::to_string(ifilter->height);
    args += ":pix_fmt=" + std::to_string(ifilter->format);
    args += ":time_base=" + std::to_string(tb.num) + "/" + std::to_string(tb.den);
    args += ":pixel_aspect=" + std::to_string(sar.num) + "/" + std::to_string(sar.den);
    args += ":sws_param=flags=" + std::to_string(SWS_BILINEAR + ((ist->dec_ctx->flags & AV_CODEC_FLAG_BITEXACT) ? SWS_BITEXACT : 0));
    if (fr.num && fr.den)
        args += ":frame_rate==" + std::to_string(fr.num) + "/" + std::to_string(fr.den);
    snprintf(name, sizeof(name), "graph %d input from stream %d:%d", fg->index,
             ist->file_index, ist->st->index);

    if ((ret = avfilter_graph_create_filter(&ifilter->filter, buffer_filt, name,
                                            args.c_str(), NULL, fg->graph)) < 0)
        goto fail;
    par->hw_frames_ctx = ifilter->hw_frames_ctx;
    ret = av_buffersrc_parameters_set(ifilter->filter, par);
    if (ret < 0)
        goto fail;
    av_freep(&par);
    last_filter = ifilter->filter;

    if (ist->autorotate)
    {
        double theta = get_rotation(ist->st);

        if (fabs(theta - 90) < 1.0)
        {
            ret = insert_filter(&last_filter, &pad_idx, "transpose", "clock");
        }
        else if (fabs(theta - 180) < 1.0)
        {
            ret = insert_filter(&last_filter, &pad_idx, "hflip", NULL);
            if (ret < 0)
                return ret;
            ret = insert_filter(&last_filter, &pad_idx, "vflip", NULL);
        }
        else if (fabs(theta - 270) < 1.0)
        {
            ret = insert_filter(&last_filter, &pad_idx, "transpose", "cclock");
        }
        else if (fabs(theta) > 1.0)
        {
            char rotate_buf[64];
            snprintf(rotate_buf, sizeof(rotate_buf), "%f*PI/180", theta);
            ret = insert_filter(&last_filter, &pad_idx, "rotate", rotate_buf);
        }
        if (ret < 0)
            return ret;
    }

    if (do_deinterlace)
    {
        AVFilterContext *yadif;

        snprintf(name, sizeof(name), "deinterlace_in_%d_%d",
                 ist->file_index, ist->st->index);
        if ((ret = avfilter_graph_create_filter(&yadif,
                                                avfilter_get_by_name("yadif"),
                                                name, "", NULL,
                                                fg->graph)) < 0)
            return ret;

        if ((ret = avfilter_link(last_filter, 0, yadif, 0)) < 0)
            return ret;

        last_filter = yadif;
    }

    snprintf(name, sizeof(name), "trim_in_%d_%d",
             ist->file_index, ist->st->index);
    if (copy_ts)
    {
        tsoffset = f->start_time == AV_NOPTS_VALUE ? 0 : f->start_time;
        if (!start_at_zero && f->ctx->start_time != AV_NOPTS_VALUE)
            tsoffset += f->ctx->start_time;
    }
    ret = insert_trim(((f->start_time == AV_NOPTS_VALUE) || !f->accurate_seek) ? AV_NOPTS_VALUE : tsoffset, f->recording_time,
                      &last_filter, &pad_idx, name);
    if (ret < 0)
        return ret;

    if ((ret = avfilter_link(last_filter, 0, in->filter_ctx, in->pad_idx)) < 0)
        return ret;
    return 0;
fail:
    av_freep(&par);

    return ret;
}

static int configure_input_audio_filter(FilterGraph *fg, InputFilter *ifilter,
                                        AVFilterInOut *in)
{
    AVFilterContext *last_filter;
    const AVFilter *abuffer_filt = avfilter_get_by_name("abuffer");
    InputStream *ist = ifilter->ist;
    InputFile *f = input_files[ist->file_index];
    std::string args;
    char name[255];
    int ret, pad_idx = 0;
    int64_t tsoffset = 0;

    if (ist->dec_ctx->codec_type != AVMEDIA_TYPE_AUDIO)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot connect audio filter to non audio input\n");
        return AVERROR(EINVAL);
    }

    args = "time_base=1/";
    args += std::to_string(ifilter->sample_rate);
    args += ":sample_rate=" + std::to_string(ifilter->sample_rate);
    args += ":sample_fmt=";
    args += av_get_sample_fmt_name((AVSampleFormat)ifilter->format);
    if (ifilter->channel_layout)
        args += ":channel_layout=0x" + std::to_string(ifilter->channel_layout);
    else
        args += ":channels=" + std::to_string(ifilter->channels);
    snprintf(name, sizeof(name), "graph_%d_in_%d_%d", fg->index,
             ist->file_index, ist->st->index);

    if ((ret = avfilter_graph_create_filter(&ifilter->filter, abuffer_filt,
                                            name, args.c_str(), NULL,
                                            fg->graph)) < 0)
        return ret;
    last_filter = ifilter->filter;

#define AUTO_INSERT_FILTER_INPUT(opt_name, filter_name, arg)                         \
    do                                                                               \
    {                                                                                \
        AVFilterContext *filt_ctx;                                                   \
                                                                                     \
        av_log(NULL, AV_LOG_INFO, opt_name " is forwarded to lavfi "                 \
                                           "similarly to -af " filter_name "=%s.\n", \
               arg);                                                                 \
                                                                                     \
        snprintf(name, sizeof(name), "graph_%d_%s_in_%d_%d",                         \
                 fg->index, filter_name, ist->file_index, ist->st->index);           \
        ret = avfilter_graph_create_filter(&filt_ctx,                                \
                                           avfilter_get_by_name(filter_name),        \
                                           name, arg, NULL, fg->graph);              \
        if (ret < 0)                                                                 \
            return ret;                                                              \
                                                                                     \
        ret = avfilter_link(last_filter, 0, filt_ctx, 0);                            \
        if (ret < 0)                                                                 \
            return ret;                                                              \
                                                                                     \
        last_filter = filt_ctx;                                                      \
    } while (0)

    if (audio_sync_method > 0)
    {
        char args[256] = {0};

        av_strlcatf(args, sizeof(args), "async=%d", audio_sync_method);
        if (audio_drift_threshold != 0.1)
            av_strlcatf(args, sizeof(args), ":min_hard_comp=%f", audio_drift_threshold);
        if (!fg->reconfiguration)
            av_strlcatf(args, sizeof(args), ":first_pts=0");
        AUTO_INSERT_FILTER_INPUT("-async", "aresample", args);
    }

    //     if (ost->audio_channels_mapped) {
    //         int i;
    //         AVBPrint pan_buf;
    //         av_bprint_init(&pan_buf, 256, 8192);
    //         av_bprintf(&pan_buf, "0x%"PRIx64,
    //                    av_get_default_channel_layout(ost->audio_channels_mapped));
    //         for (i = 0; i < ost->audio_channels_mapped; i++)
    //             if (ost->audio_channels_map[i] != -1)
    //                 av_bprintf(&pan_buf, ":c%d=c%d", i, ost->audio_channels_map[i]);
    //         AUTO_INSERT_FILTER_INPUT("-map_channel", "pan", pan_buf.str);
    //         av_bprint_finalize(&pan_buf, NULL);
    //     }

    if (audio_volume != 256)
    {
        char args[256];

        av_log(NULL, AV_LOG_WARNING, "-vol has been deprecated. Use the volume "
                                     "audio filter instead.\n");

        snprintf(args, sizeof(args), "%f", audio_volume / 256.);
        AUTO_INSERT_FILTER_INPUT("-vol", "volume", args);
    }

    snprintf(name, sizeof(name), "trim for input stream %d:%d",
             ist->file_index, ist->st->index);
    if (copy_ts)
    {
        tsoffset = f->start_time == AV_NOPTS_VALUE ? 0 : f->start_time;
        if (!start_at_zero && f->ctx->start_time != AV_NOPTS_VALUE)
            tsoffset += f->ctx->start_time;
    }
    ret = insert_trim(((f->start_time == AV_NOPTS_VALUE) || !f->accurate_seek) ? AV_NOPTS_VALUE : tsoffset, f->recording_time,
                      &last_filter, &pad_idx, name);
    if (ret < 0)
        return ret;

    if ((ret = avfilter_link(last_filter, 0, in->filter_ctx, in->pad_idx)) < 0)
        return ret;

    return 0;
}

static int configure_input_filter(FilterGraph *fg, InputFilter *ifilter,
                                  AVFilterInOut *in)
{
    if (!ifilter->ist->dec)
    {
        av_log(NULL, AV_LOG_ERROR,
               "No decoder for stream #%d:%d, filtering impossible\n",
               ifilter->ist->file_index, ifilter->ist->st->index);
        return AVERROR_DECODER_NOT_FOUND;
    }
    switch (avfilter_pad_get_type(in->filter_ctx->input_pads, in->pad_idx))
    {
    case AVMEDIA_TYPE_VIDEO:
        return configure_input_video_filter(fg, ifilter, in);
    case AVMEDIA_TYPE_AUDIO:
        return configure_input_audio_filter(fg, ifilter, in);
    default:
        av_assert0(0);
    }
}

static void cleanup_filtergraph(FilterGraph *fg)
{
    int i;
    for (i = 0; i < fg->outputs.size(); i++)
        fg->outputs[i]->filter = (AVFilterContext *)NULL;
    for (i = 0; i < fg->inputs.size(); i++)
        fg->inputs[i]->filter = (AVFilterContext *)NULL;
    avfilter_graph_free(&fg->graph);
}

int configure_filtergraph(FilterGraph *fg)
{
    AVFilterInOut *inputs, *outputs, *cur;
    int ret, i, simple = filtergraph_is_simple(fg);
    const char *graph_desc = simple ? fg->outputs[0]->ost->avfilter : fg->graph_desc;

    cleanup_filtergraph(fg);
    if (!(fg->graph = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);

    if (simple)
    {
        OutputStream *ost = fg->outputs[0]->ost;
        char args[512];
        AVDictionaryEntry *e = NULL;

        fg->graph->nb_threads = filter_nbthreads;

        args[0] = 0;
        while ((e = av_dict_get(ost->sws_dict, "", e,
                                AV_DICT_IGNORE_SUFFIX)))
        {
            av_strlcatf(args, sizeof(args), "%s=%s:", e->key, e->value);
        }
        if (strlen(args))
            args[strlen(args) - 1] = 0;
        fg->graph->scale_sws_opts = av_strdup(args);

        args[0] = 0;
        while ((e = av_dict_get(ost->swr_opts, "", e,
                                AV_DICT_IGNORE_SUFFIX)))
        {
            av_strlcatf(args, sizeof(args), "%s=%s:", e->key, e->value);
        }
        if (strlen(args))
            args[strlen(args) - 1] = 0;
        av_opt_set(fg->graph, "aresample_swr_opts", args, 0);

        args[0] = '\0';
        while ((e = av_dict_get(fg->outputs[0]->ost->resample_opts, "", e,
                                AV_DICT_IGNORE_SUFFIX)))
        {
            av_strlcatf(args, sizeof(args), "%s=%s:", e->key, e->value);
        }
        if (strlen(args))
            args[strlen(args) - 1] = '\0';

        e = av_dict_get(ost->encoder_opts, "threads", NULL, 0);
        if (e)
            av_opt_set(fg->graph, "threads", e->value, 0);
    }
    else
    {
        fg->graph->nb_threads = filter_complex_nbthreads;
    }

    if ((ret = avfilter_graph_parse2(fg->graph, graph_desc, &inputs, &outputs)) < 0)
        goto fail;

    if (filter_hw_device || hw_device_ctx)
    {
        AVBufferRef *device = filter_hw_device ? filter_hw_device->device_ref
                                               : hw_device_ctx;
        for (i = 0; i < fg->graph->nb_filters; i++)
        {
            fg->graph->filters[i]->hw_device_ctx = av_buffer_ref(device);
            if (!fg->graph->filters[i]->hw_device_ctx)
            {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        }
    }

    if (simple && (!inputs || inputs->next || !outputs || outputs->next))
    {
        const char *num_inputs;
        const char *num_outputs;
        if (!outputs)
        {
            num_outputs = "0";
        }
        else if (outputs->next)
        {
            num_outputs = ">1";
        }
        else
        {
            num_outputs = "1";
        }
        if (!inputs)
        {
            num_inputs = "0";
        }
        else if (inputs->next)
        {
            num_inputs = ">1";
        }
        else
        {
            num_inputs = "1";
        }
        av_log(NULL, AV_LOG_ERROR, "Simple filtergraph '%s' was expected "
                                   "to have exactly 1 input and 1 output."
                                   " However, it had %s input(s) and %s output(s)."
                                   " Please adjust, or use a complex filtergraph (-filter_complex) instead.\n",
               graph_desc, num_inputs, num_outputs);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    for (cur = inputs, i = 0; cur; cur = cur->next, i++)
        if ((ret = configure_input_filter(fg, fg->inputs[i], cur)) < 0)
        {
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            goto fail;
        }
    avfilter_inout_free(&inputs);

    for (cur = outputs, i = 0; cur; cur = cur->next, i++)
        configure_output_filter(fg, fg->outputs[i], cur);
    avfilter_inout_free(&outputs);

    if ((ret = avfilter_graph_config(fg->graph, NULL)) < 0)
        goto fail;

    /* limit the lists of allowed formats to the ones selected, to
     * make sure they stay the same if the filtergraph is reconfigured later */
    for (i = 0; i < fg->nb_outputs; i++)
    {
        OutputFilter *ofilter = fg->outputs[i];
        AVFilterContext *sink = ofilter->filter;

        ofilter->format = av_buffersink_get_format(sink);

        ofilter->width = av_buffersink_get_w(sink);
        ofilter->height = av_buffersink_get_h(sink);

        ofilter->sample_rate = av_buffersink_get_sample_rate(sink);
        ofilter->channel_layout = av_buffersink_get_channel_layout(sink);
    }

    fg->reconfiguration = 1;

    for (i = 0; i < fg->nb_outputs; i++)
    {
        OutputStream *ost = fg->outputs[i]->ost;
        if (!ost->enc)
        {
            /* identical to the same check in ffmpeg.c, needed because
               complex filter graphs are initialized earlier */
            av_log(NULL, AV_LOG_ERROR, "Encoder (codec %s) not found for output stream #%d:%d\n",
                   avcodec_get_name(ost->st->codecpar->codec_id), ost->file_index, ost->index);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        if (ost->enc->type == AVMEDIA_TYPE_AUDIO &&
            !(ost->enc->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
            av_buffersink_set_frame_size(ost->filter->filter,
                                         ost->enc_ctx->frame_size);
    }

    for (i = 0; i < fg->inputs.size(); i++)
    {
        while (av_fifo_size(fg->inputs[i]->frame_queue))
        {
            AVFrame *tmp;
            av_fifo_generic_read(fg->inputs[i]->frame_queue, &tmp, sizeof(tmp), NULL);
            ret = av_buffersrc_add_frame(fg->inputs[i]->filter, tmp);
            av_frame_free(&tmp);
            if (ret < 0)
                goto fail;
        }
    }

    /* send the EOFs for the finished inputs */
    for (i = 0; i < fg->inputs.size(); i++)
    {
        if (fg->inputs[i]->eof)
        {
            ret = av_buffersrc_add_frame(fg->inputs[i]->filter, NULL);
            if (ret < 0)
                goto fail;
        }
    }

    /* process queued up subtitle packets */
    for (i = 0; i < fg->inputs.size(); i++)
    {
        InputStream *ist = fg->inputs[i]->ist;
        if (ist->sub2video.sub_queue && ist->sub2video.frame)
        {
            while (av_fifo_size(ist->sub2video.sub_queue))
            {
                AVSubtitle tmp;
                av_fifo_generic_read(ist->sub2video.sub_queue, &tmp, sizeof(tmp), NULL);
                sub2video_update(ist, &tmp);
                avsubtitle_free(&tmp);
            }
        }
    }

    return 0;

fail:
    cleanup_filtergraph(fg);
    return ret;
}

int ifilter_parameters_from_frame(InputFilter *ifilter, const AVFrame *frame)
{
    av_buffer_unref(&ifilter->hw_frames_ctx);

    ifilter->format = frame->format;

    ifilter->width = frame->width;
    ifilter->height = frame->height;
    ifilter->sample_aspect_ratio = frame->sample_aspect_ratio;

    ifilter->sample_rate = frame->sample_rate;
    ifilter->channels = frame->channels;
    ifilter->channel_layout = frame->channel_layout;

    if (frame->hw_frames_ctx)
    {
        ifilter->hw_frames_ctx = av_buffer_ref(frame->hw_frames_ctx);
        if (!ifilter->hw_frames_ctx)
            return AVERROR(ENOMEM);
    }

    return 0;
}

int ist_in_filtergraph(FilterGraph *fg, InputStream *ist)
{
    int i;
    for (i = 0; i < fg->inputs.size(); i++)
        if (fg->inputs[i]->ist == ist)
            return 1;
    return 0;
}

int filtergraph_is_simple(FilterGraph *fg)
{
    return !fg->graph_desc;
}

///////////////////////////////////////////////

// Filters can be configured only if the formats of all inputs are known.
int ifilter_has_all_input_formats(FilterGraph *fg)
{
    int i;
    for (i = 0; i < fg->inputs.size(); i++)
    {
        if (fg->inputs[i]->format < 0 && (fg->inputs[i]->type == AVMEDIA_TYPE_AUDIO ||
                                          fg->inputs[i]->type == AVMEDIA_TYPE_VIDEO))
            return 0;
    }
    return 1;
}

void ifilter_parameters_from_codecpar(InputFilter *ifilter, AVCodecParameters *par)
{
    // We never got any input. Set a fake format, which will
    // come from libavformat.
    ifilter->format = par->format;
    ifilter->sample_rate = par->sample_rate;
    ifilter->channels = par->channels;
    ifilter->channel_layout = par->channel_layout;
    ifilter->width = par->width;
    ifilter->height = par->height;
    ifilter->sample_aspect_ratio = par->sample_aspect_ratio;
}

int ifilter_send_eof(InputFilter *ifilter, int64_t pts)
{
    int ret;

    ifilter->eof = 1;

    if (ifilter->filter)
    {
        ret = av_buffersrc_close(ifilter->filter, pts, AV_BUFFERSRC_FLAG_PUSH);
        if (ret < 0)
            return ret;
    }
    else
    {
        // the filtergraph was never configured
        if (ifilter->format < 0)
            ifilter_parameters_from_codecpar(ifilter, ifilter->ist->st->codecpar);
        if (ifilter->format < 0 && (ifilter->type == AVMEDIA_TYPE_AUDIO || ifilter->type == AVMEDIA_TYPE_VIDEO))
        {
            av_log(NULL, AV_LOG_ERROR, "Cannot determine format of input stream %d:%d after EOF\n", ifilter->ist->file_index, ifilter->ist->st->index);
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

int ifilter_send_frame(InputFilter *ifilter, AVFrame *frame)
{
    FilterGraph *fg = ifilter->graph;
    int need_reinit, ret, i;

    /* determine if the parameters for this input changed */
    need_reinit = ifilter->format != frame->format;

    switch (ifilter->ist->st->codecpar->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        need_reinit |= ifilter->sample_rate != frame->sample_rate ||
                       ifilter->channels != frame->channels ||
                       ifilter->channel_layout != frame->channel_layout;
        break;
    case AVMEDIA_TYPE_VIDEO:
        need_reinit |= ifilter->width != frame->width ||
                       ifilter->height != frame->height;
        break;
    }

    if (!ifilter->ist->reinit_filters && fg->graph)
        need_reinit = 0;

    if (!!ifilter->hw_frames_ctx != !!frame->hw_frames_ctx ||
        (ifilter->hw_frames_ctx && ifilter->hw_frames_ctx->data != frame->hw_frames_ctx->data))
        need_reinit = 1;

    if (need_reinit)
    {
        ret = ifilter_parameters_from_frame(ifilter, frame);
        if (ret < 0)
            return ret;
    }

    /* (re)init the graph if possible, otherwise buffer the frame and return */
    if (need_reinit || !fg->graph)
    {
        for (i = 0; i < fg->inputs.size(); i++)
        {
            if (!ifilter_has_all_input_formats(fg))
            {
                AVFrame *tmp = av_frame_clone(frame);
                if (!tmp)
                    return AVERROR(ENOMEM);
                av_frame_unref(frame);

                if (!av_fifo_space(ifilter->frame_queue))
                {
                    ret = av_fifo_realloc2(ifilter->frame_queue, 2 * av_fifo_size(ifilter->frame_queue));
                    if (ret < 0)
                    {
                        av_frame_free(&tmp);
                        return ret;
                    }
                }
                av_fifo_generic_write(ifilter->frame_queue, &tmp, sizeof(tmp), NULL);
                return 0;
            }
        }

        ret = reap_filters(1);
        if (ret < 0 && ret != AVERROR_EOF)
        {
            AVException::log_error(AV_LOG_ERROR, "Error while filtering: %s", ret);
            return ret;
        }

        ret = configure_filtergraph(fg);
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Error reinitializing filters!\n");
            return ret;
        }
    }

    ret = av_buffersrc_add_frame_flags(ifilter->filter, frame, AV_BUFFERSRC_FLAG_PUSH);
    if (ret < 0)
    {
        if (ret != AVERROR_EOF)
            AVException::log_error(AV_LOG_ERROR, "Error while filtering: %s", ret);
        return ret;
    }

    return 0;
}

double get_rotation(AVStream *st)
{
    uint8_t *displaymatrix = av_stream_get_side_data(st,
                                                     AV_PKT_DATA_DISPLAYMATRIX, NULL);
    double theta = 0;
    if (displaymatrix)
        theta = -av_display_rotation_get((int32_t *)displaymatrix);

    theta -= 360 * floor(theta / 360 + 0.9 / 360);

    if (fabs(theta - 90 * round(theta / 90)) > 2)
        av_log(NULL, AV_LOG_WARNING, "Odd rotation angle.\n"
                                     "If you want to help, upload a sample "
                                     "of this file to ftp://upload.ffmpeg.org/incoming/ "
                                     "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)");

    return theta;
}

/**
 * Get and encode new output from any of the filtergraphs, without causing
 * activity.
 *
 * @return  0 for success, <0 for severe errors
 */
int reap_filters(int flush)
{
    AVFrame *filtered_frame = NULL;
    int i;

    /* Reap all buffers present in the buffer sinks */
    for (i = 0; i < nb_output_streams; i++)
    {
        OutputStream *ost = output_streams[i];
        OutputFile *of = output_files[ost->file_index];
        AVFilterContext *filter;
        AVCodecContext *enc = ost->enc_ctx;
        int ret = 0;

        if (!ost->filter || !ost->filter->graph->graph)
            continue;
        filter = ost->filter->filter;

        if (!ost->initialized)
        {
            char error[1024] = "";
            ret = init_output_stream(ost, error, sizeof(error));
            if (ret < 0)
                AVException::log_error(true,
                                       "Error initializing output stream %d:%d -- %s\n",
                                       2, ost->file_index, ost->index, error);
        }

        if (!ost->filtered_frame && !(ost->filtered_frame = av_frame_alloc()))
        {
            return AVERROR(ENOMEM);
        }
        filtered_frame = ost->filtered_frame;

        while (true)
        {
            double float_pts = AV_NOPTS_VALUE; // this is identical to filtered_frame.pts but with higher precision
            ret = av_buffersink_get_frame_flags(filter, filtered_frame,
                                                AV_BUFFERSINK_FLAG_NO_REQUEST);
            if (ret < 0)
            {
                if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                {
                    AVException::log_error(AV_LOG_WARNING,
                                           "Error in av_buffersink_get_frame_flags(): %s\n",
                                           ret);
                }
                else if (flush && ret == AVERROR_EOF)
                {
                    if (av_buffersink_get_type(filter) == AVMEDIA_TYPE_VIDEO)
                        do_video_out(of, ost, NULL, AV_NOPTS_VALUE);
                }
                break;
            }
            if (ost->finished)
            {
                av_frame_unref(filtered_frame);
                continue;
            }
            if (filtered_frame->pts != AV_NOPTS_VALUE)
            {
                int64_t start_time = (of->start_time == AV_NOPTS_VALUE) ? 0 : of->start_time;
                AVRational filter_tb = av_buffersink_get_time_base(filter);
                AVRational tb = enc->time_base;
                int extra_bits = av_clip(29 - av_log2(tb.den), 0, 16);

                tb.den <<= extra_bits;
                float_pts =
                    av_rescale_q(filtered_frame->pts, filter_tb, tb) -
                    av_rescale_q(start_time, AV_TIME_BASE_Q, tb);
                float_pts /= 1 << extra_bits;
                // avoid exact midoints to reduce the chance of rounding differences, this can be removed in case the fps code is changed to work with integers
                float_pts += FFSIGN(float_pts) * 1.0 / (1 << 17);

                filtered_frame->pts =
                    av_rescale_q(filtered_frame->pts, filter_tb, enc->time_base) -
                    av_rescale_q(start_time, AV_TIME_BASE_Q, enc->time_base);
            }
            //if (ost->source_index >= 0)
            //    *filtered_frame= *input_streams[ost->source_index]->decoded_frame; //for me_threshold

            switch (av_buffersink_get_type(filter))
            {
            case AVMEDIA_TYPE_VIDEO:
                if (!ost->frame_aspect_ratio.num)
                    enc->sample_aspect_ratio = filtered_frame->sample_aspect_ratio;

                // if (debug_ts)
                // {
                //     av_log(NULL, AV_LOG_INFO, "filter -> pts:%s pts_time:%s exact:%f time_base:%d/%d\n",
                //            av_ts2str(filtered_frame->pts), av_ts2timestr(filtered_frame->pts, &enc->time_base),
                //            float_pts,
                //            enc->time_base.num, enc->time_base.den);
                // }

                do_video_out(of, ost, filtered_frame, float_pts);
                break;
            case AVMEDIA_TYPE_AUDIO:
                if (!(enc->codec->capabilities & AV_CODEC_CAP_PARAM_CHANGE) &&
                    enc->channels != filtered_frame->channels)
                {
                    av_log(NULL, AV_LOG_ERROR,
                           "Audio filter graph output is not normalized and encoder does not support parameter changes\n");
                    break;
                }
                do_audio_out(of, ost, filtered_frame);
                break;
            default:
                // TODO support subtitle filters
                av_assert0(0);
            }

            av_frame_unref(filtered_frame);
        }
    }

    return 0;
}
