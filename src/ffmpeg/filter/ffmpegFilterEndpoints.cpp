#include "ffmpegFilterEndPoints.h"

extern "C" {
#include <libavfilter/avfilter.h>
}

#include <memory>

using namespace ffmpeg;
using namespace filter;

EndpointBase::EndpointBase(Graph &parent, const AVMediaType type, const AVRational &tb)
    : Base(parent), MediaHandler(type, tb), prefilter_context(NULL), prefilter_pad(0) {}
EndpointBase::EndpointBase(Graph &parent, const IMediaHandler &mdev)
    : Base(parent), MediaHandler(mdev), prefilter_pad(0) {}
EndpointBase::~EndpointBase() {}

void EndpointBase::purge()
{
  context = NULL;
  prefilter_context = NULL;
}

const std::string &EndpointBase::setPrefilter() { return prefilter_desc; }

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
      throw ffmpegException("[ffmpeg::filter::EndpointBase::setPrefilter] Failed to parse the prefilter chain description: %s.",desc.c_str());

    bool fail = !(ins && !ins->next && outs && !outs->next);
    avfilter_inout_free(&ins);
    avfilter_inout_free(&outs);
    if (fail)
      throw ffmpegException("[ffmpeg::filter::EndpointBase::setPrefilter] Failed to parse the prefilter chain description.");

    av_log(NULL, AV_LOG_INFO, "[ffmpeg::filter::EndpointBase::setPrefilter] Prefilter successfully set: %s.\n", desc.c_str());
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
AVFilterContext *EndpointBase::configure_prefilter(bool issrc)
{
  av_log(NULL, AV_LOG_INFO, "[ffmpeg::filter::EndpointBase::configure_prefilter] prefilter_desc=%s\n", prefilter_desc.c_str());

  // no prefilter set, just return the endpoint filter
  if (prefilter_desc.empty())
  {
    prefilter_context = context;
    prefilter_pad = 0;
    return context;
  }

  // Parse the filter chain to the actual filter graph
  AVFilterInOut *ins = NULL, *outs = NULL;
  if ((avfilter_graph_parse2(context->graph, prefilter_desc.c_str(), &ins, &outs)) < 0)
    throw ffmpegException("[ffmpeg::filter::EndpointBase::configure_prefilter] Failed to parse the prefilter chain description.");

  // set unique_ptrs to auto delete the pointer when going out of scope
  auto avFilterInOutFree = [](AVFilterInOut *ptr) { avfilter_inout_free(&ptr); };
  std::unique_ptr<AVFilterInOut, decltype(avFilterInOutFree)> pin(ins, avFilterInOutFree), pout(outs, avFilterInOutFree);

  if (issrc) // link its input to ep's output and return the input
  {
    if (avfilter_link(context, 0, ins->filter_ctx, ins->pad_idx) < 0)
      throw ffmpegException("[ffmpeg::filter::EndpointBase::configure_prefilter] Failed to link prefilter to source.");
    prefilter_context = outs->filter_ctx;
    prefilter_pad = outs->pad_idx;
  }
  else // link its ouptut to ep's input and return the output
  {
    if (avfilter_link(outs->filter_ctx, outs->pad_idx, context, 0) < 0)
      throw ffmpegException("[ffmpeg::filter::EndpointBase::configure_prefilter] Failed to link prefilter to sink.");
    prefilter_context = ins->filter_ctx;
    prefilter_pad = ins->pad_idx;
  }
  return prefilter_context;
}

void EndpointBase::link(AVFilterContext *other, const unsigned otherpad, const unsigned pad, const bool issrc)
{
  if (!prefilter_context)
    throw ffmpegException("Filter context has not been configured.");
  if (!other)
    throw ffmpegException("The other filter context not given (NULL).");
  if (prefilter_context->graph != other->graph)
    throw ffmpegException("Filter contexts must be for the same AVFilterGraph.");

  int ret;
  if (issrc)
    ret = avfilter_link(prefilter_context, prefilter_pad, other, otherpad);
  else
    ret = avfilter_link(other, otherpad, prefilter_context, prefilter_pad);
  if (ret < 0)
  {
    if (ret == AVERROR(EINVAL))
      throw ffmpegException("Failed to link filters (invalid parameter).");
    else if (ret == AVERROR(ENOMEM))
      throw ffmpegException("Failed to link filters (could not allocate memory).");
    else
      throw ffmpegException("Failed to link filters.");
  }
}