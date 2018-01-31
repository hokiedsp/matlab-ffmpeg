#include "ffmpegFilterGraph.h"

// #include <stdint.h>

extern "C" {
// #include "libavfilter/avfilter.h"
// #include "libavresample/avresample.h"
// #include "libavutil/avassert.h"
}

#include <algorithm>

using namespace ffmpeg;
using namespace filter;

Graph::Graph(const std::string &filtdesc)
{
  if (filtdesc.size())
    parse(filtdesc);
}
Graph::~Graph()
{
  destroy(true);
}

void Graph::destroy(const bool complete)
{
  for (auto p = inputs.begin(); p != inputs.end(); ++p)
  {
    if (p->second.filter)
    {
      if (complete)
      {
        delete p->second.filter;
        p->second.filter = NULL;
      }
      else
        p->second.filter->destroy();
    }
  }
  for (auto p = outputs.begin(); p != outputs.end(); ++p)
  {
    if (p->second.filter)
    {
      if (complete)
      {
        delete p->second.filter;
        p->second.filter = NULL;
      }
      else
        p->second.filter->destroy();
    }
  }
  avfilter_graph_free(&graph);
}

void Graph::parse(const std::string &new_desc)
{
  // allocate new filter graph
  AVFilterGraph *temp_graph;
  if (!(temp_graph = avfilter_graph_alloc()))
    throw ffmpegException(AVERROR(ENOMEM));

  // Parse the string to get I/O endpoints
  AVFilterInOut *ins = NULL, *outs = NULL;
  if ((avfilter_graph_parse2(temp_graph, new_desc.c_str(), &ins, &outs)) < 0)
  {
    avfilter_graph_free(&temp_graph);
    throw ffmpegException("Failed to parse the filter graph description.");
  }

  // clear the existing filtergraph
  destroy(true); // destroy AVFilterContext as well as the ffmpeg::filter::* objects
  outputs.clear();
  inputs.clear();

  // update the filter graph
  graph_desc = new_desc;

  // create source filter placeholder and mark its type
  for (AVFilterInOut *cur = ins; cur; cur = cur->next)
    inputs[cur->name] = SourceInfo({avfilter_pad_get_type(cur->filter_ctx->input_pads, cur->pad_idx), NULL, cur->filter_ctx, cur->pad_idx});
  avfilter_inout_free(&ins);

  // create sink filter placeholder and mark its type
  for (AVFilterInOut *cur = outs; cur; cur = cur->next)
    outputs[cur->name] = SinkInfo({avfilter_pad_get_type(cur->filter_ctx->output_pads, cur->pad_idx), NULL, cur->filter_ctx, cur->pad_idx});
  avfilter_inout_free(&outs);

  avfilter_graph_free(&temp_graph);
}

template <typename F, typename M>
F *Graph::find_filter(M &map, AVMediaType type)
{
  if (map.empty())
    throw ffmpegException("Cannot find an endpoint on the filter graph: No connecting pad available.");

  // find the first source of the stream type
  auto match = map.begin();
  for (; match != map.end() && match->second.type != type; ++match)
    ;
  if (match != map.end())
    throw ffmpegException("Cannot find any %s endpoint on the filter graph.", av_get_media_type_string(type));

  return match->second.filter;
}

template <typename EP, typename VEP, typename AEP, typename... Args>
void Graph::assign_endpoint(EP *&ep, AVMediaType type, Args... args)
{
  // if already defined, destruct existing
  if (ep)
    delete ep;

  // create new filter
  switch (type)
  {
  case AVMEDIA_TYPE_VIDEO:
    ep = new VEP(*this, args...);
    break;
  case AVMEDIA_TYPE_AUDIO:
    ep = new AEP(*this, args...);
    break;
  default:
    ep = NULL;
    throw ffmpegException("Only video and audio filters are supported at this time.");
  }
}

template <typename B>
void Graph::assignSource(B &buf)
{
  AVMediaType type = B.getMediaType();
  auto src = Graph::find_filter<SourceBase>(inputs, type);
  Graph::assign_endpoint<SourceBase, VideoSource, AudioSource>(src, type, buf);
}

template <typename B>
void Graph::assignSource(const std::string &name, B &buf)
{
  auto node = inputs.at(name); // throws excepetion if doesn't exist
  Graph::assign_endpoint<SourceBase, VideoSource, AudioSource>(node.filter, node.type, buf);
}

template <typename B>
void Graph::assignSink(B &buf)
{
  AVMediaType type = buf.getMediaType();
  auto src = Graph::find_filter<SinkBase>(outputs, type);
  Graph::assign_endpoint<SourceBase, VideoSource, AudioSource>(src, type, buf);
}

template <typename B>
void Graph::assignSink(const std::string &name, B &buf)
{
  auto node = outputs.at(name); // throws excepetion if doesn't exist
  Graph::assign_endpoint<SinkBase, VideoSink, AudioSink>(node.filter, node.type, buf);
}

void Graph::configure()
{
  try
  {
    // configure source filters
    for (auto in = inputs.begin(); in != inputs.end(); ++in)
    {
      SourceBase *src = in->second.filter;
      if (!src)
        throw ffmpegException("Source filter is not set.");

      // configure filter
      src->configure(in->first);

      // link filter
      src->link(in->second.other, in->second.otherpad);
    }

    // configure sink filters
    for (auto out = outputs.begin(); out != outputs.end(); ++out)
    {
      SinkBase *sink = out->second.filter;
      if (!sink)
        throw ffmpegException("Sink filter is not set.");

      // configure filter
      sink->configure(out->first);

      // link the sink to the last filter
      sink->link(out->second.other, out->second.otherpad);
    }

    // finalize the graph
    if (avfilter_graph_config(graph, NULL) < 0)
      throw ffmpegException("Failed to finalize the filter graph.");

    /* limit the lists of allowed formats to the ones selected, to
     * make sure they stay the same if the filtergraph is reconfigured later */
    for (auto ofilter = outputs.begin(); ofilter != outputs.end(); ++ofilter)
      ofilter->second.filter->sync();

    // fg->reconfiguration = 1;
  }
  catch (...)
  {
    destroy(false);
    throw;
  }
}

// used to append filters for autorotate feature
int Graph::insert_filter(AVFilterContext *&last_filter, int &pad_idx,
                         const std::string &filter_name, const std::string &args)
{
  AVFilterGraph *graph = last_filter->graph;
  AVFilterContext *ctx;
  int ret;

  ret = avfilter_graph_create_filter(&ctx,
                                     avfilter_get_by_name(filter_name.c_str()),
                                     filter_name.c_str(), args.c_str(), NULL, graph);
  if (ret < 0)
    return ret;

  ret = avfilter_link(last_filter, pad_idx, ctx, 0);
  if (ret < 0)
    return ret;

  last_filter = ctx;
  pad_idx = 0;
  return 0;
}
