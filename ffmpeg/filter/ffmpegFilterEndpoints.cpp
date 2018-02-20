#include "ffmpegFilterEndPoints.h"

extern "C" {
#include <libavfilter/avfilter.h>
}

#include <memory>

using namespace ffmpeg;
using namespace filter;

EndpointBase::EndpointBase(Graph &parent, const AVMediaType type, const AVRational &tb)
    : Base(parent), MediaHandler(type, tb), prefilter_pad(0) {}
EndpointBase::EndpointBase(Graph &parent, const IMediaHandler &mdev)
    : Base(parent), MediaHandler(mdev), prefilter_pad(0) {}
EndpointBase::~EndpointBase() {}

/**
   * /brief   Register the prefilter chain description
   * 
   * This function takes a prefilter chain description, creates a dummy filter
   * graph to verify that it is a SISO graph.
   * 
   * \param[in] desc Prefilter chain description
   * \thrpws ffmpegException if fails to parse
   */
void EndpointBase::setPrefilter(const std::string &desc)
{
    if (desc.size())
    {
        // allocate new filter graph
        AVFilterGraph *temp_graph;
        if (!(temp_graph = avfilter_graph_alloc()))
            throw ffmpegException(AVERROR(ENOMEM));

        // set unique_ptrs to auto delete the pointer when going out of scope
        auto avFilterGraphFree = [](AVFilterGraph *graph) { avfilter_graph_free(&graph); };
        std::unique_ptr<AVFilterGraph, decltype(avFilterGraphFree)> ptemp(temp_graph, avFilterGraphFree);

        // Parse the string to get I/O endpoints
        AVFilterInOut *ins = NULL, *outs = NULL;
        if ((avfilter_graph_parse2(temp_graph, desc.c_str(), &ins, &outs)) < 0)
            throw ffmpegException("[ffmpeg::filter::EndpointBase::setPrefilter] Failed to parse the prefilter chain description.");

        bool fail = !(ins && !ins->next && outs && !outs->next);
        avfilter_inout_free(&ins);
        avfilter_inout_free(&outs);
        if (fail)
            throw ffmpegException("[ffmpeg::filter::EndpointBase::setPrefilter] Failed to parse the prefilter chain description.");
    }

    // passed, update
    prefilter_desc = desc;
}

/**
   * /brief   Add prefilter graph to the filter graph
   * 
   * This function add specified prefilter graph to the existing filter graph,
   * link to the endpoint filter \ref ep, and returns the unlinked end of the
   * prefilter chain to the caller. If no prefilter is specified, it just returns
   * the endpoint filter.
   * 
   * \param[in] ep Endpoint filter context to attach the prefilter to
   * \param[in] issrc True if \ref ep is a source filter
   * \returns \ref ep if no prefilter or the unlinked filter of the prefilter chain
   */
AVFilterContext *EndpointBase::configure_prefilter(AVFilterContext *ep, bool issrc)
{
    // Parse the filter chain to the actual filter graph
    AVFilterInOut *ins = NULL, *outs = NULL;
    if ((avfilter_graph_parse2(ep->graph, prefilter_desc.c_str(), &ins, &outs)) < 0)
        throw ffmpegException("[ffmpeg::filter::EndpointBase::configure_prefilter] Failed to parse the prefilter chain description.");

    // set unique_ptrs to auto delete the pointer when going out of scope
    auto avFilterInOutFree = [](AVFilterInOut *ptr) { avfilter_inout_free(&ptr); };
    std::unique_ptr<AVFilterInOut, decltype(avFilterInOutFree)> pin(ins, avFilterInOutFree), pout(outs, avFilterInOutFree);

    if (issrc) // link its input to ep's output and return the input
    {
        if (avfilter_link(ep, 0, ins->filter_ctx, ins->pad_idx)<0)
            throw ffmpegException("[ffmpeg::filter::EndpointBase::configure_prefilter] Failed to link prefilter to source.");
        ep = outs->filter_ctx;
        prefilter_pad = outs->pad_idx;
    }
    else // link its ouptut to ep's input and return the output
    {
        if (avfilter_link(outs->filter_ctx, outs->pad_idx, ep, 0) < 0)
            throw ffmpegException("[ffmpeg::filter::EndpointBase::configure_prefilter] Failed to link prefilter to sink.");
        ep = outs->filter_ctx;
        prefilter_pad = outs->pad_idx;
    }

    return ep;
}
