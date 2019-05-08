#pragma once

#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/fifo.h>
}

struct InputFilter
{
    AVFilterContext *filter;
    struct InputStream *ist;
    struct FilterGraph *graph;
    uint8_t *name;
    enum AVMediaType type; // AVMEDIA_TYPE_SUBTITLE for sub2video

    AVFifoBuffer *frame_queue;

    // parameters configured for this input
    int format;

    int width, height;
    AVRational sample_aspect_ratio;

    int sample_rate;
    int channels;
    uint64_t channel_layout;

    AVBufferRef *hw_frames_ctx;

    int eof;
};

struct OutputFilter
{
    AVFilterContext *filter;
    struct OutputStream *ost;
    struct FilterGraph *graph;
    uint8_t *name;

    /* temporary storage until stream maps are processed */
    AVFilterInOut *out_tmp;
    enum AVMediaType type;

    /* desired output stream properties */
    int width, height;
    AVRational frame_rate;
    int format;
    int sample_rate;
    uint64_t channel_layout;

    // those are only set if no format is specified and the encoder gives us multiple options
    int *formats;
    uint64_t *channel_layouts;
    int *sample_rates;
};

struct FilterGraph
{
    int index;
    const char *graph_desc;

    AVFilterGraph *graph;
    int reconfiguration;

    std::vector<InputFilter *> inputs;
    std::vector<OutputFilter *> outputs;
    int nb_outputs;
};

int filtergraph_is_simple(FilterGraph *fg);
void cleanup_filtergraph(FilterGraph *fg);

int configure_filtergraph(FilterGraph *fg);
int ifilter_has_all_input_formats(FilterGraph *fg);
void ifilter_parameters_from_codecpar(InputFilter *ifilter, AVCodecParameters *par);
int ifilter_send_eof(InputFilter *ifilter, int64_t pts);
int ifilter_send_frame(InputFilter *ifilter, AVFrame *frame);
int reap_filters(int flush);
