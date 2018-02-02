#include "ffmpegFilterGraph.h"

#ifndef FG_TIMEOUT_IN_MS
#define FG_TIMEOUT_IN_MS 100ms
#endif

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

/**
 * \brief Graph's thread's child threads to monitor source buffers
 */
void Graph::child_thread_fcn(AVFilterContext *src, IAVFrameSource *buf)
{

  using namespace std::chrono_literals;

  AVFrame *frame;
  do
  {
    // use timed pop to be able to terminate the thread independent of source buffer
    int ret = buf->pop(frame, FG_TIMEOUT_IN_MS);
    if (killchild)
      break;

    if (ret != AVERROR(EAGAIN))
    {
      std::unique_lock<std::mutex> queue_guard(mQ);
      Qframe.push(std::make_pair(src, frame));
    }
    if (frame)

      if (!Qframe.back().second) // eof

    cvQ.notify_one(mQ);
  } while (!killchild || !frame);
}

void Graph::thread_fcn()
{
  int ret;
  
  bool reconfigure = true;

  // * create a child thread pool to monitor source buffers
  Qframe.reserve()
  std::vector<uint_t> more_in(inputs.size());

  // std::vector<InputStream *> st_lut(fmt_ctx->nb_streams, NULL);
  // std::for_each(streams.begin(), streams.end(), [&st_lut](InputStream *is) { st_lut[is->getId()] = is; });

  // "true" if sink available
  std::vector<uint_t> more_out(outputs.size());
  bool least_one_more_out;

  try
  {
    // mutex guard, locked initially
    std::unique_lock<std::mutex> thread_guard(thread_lock);

    // read all packets
    while (!killnow)
    {
      // wait until a file is opened and the reader is unleashed
      if (status == IDLE)
      {
        thread_ready.notify_one();
        thread_ready.wait(thread_guard);
        thread_guard.unlock();
        if (killnow || status == PAUSE_RQ)
          continue;
        status = ACTIVE;
      }

      // end of status management::unlock the mutex guard
      thread_guard.unlock();

      if (reconfigure)
      {
        // craete the AVFilterGraph from Graph
        configure();

        reconfigure = false;
      }

      // monitor source buffers for incoming AVFrame

      // run the filter
      if (last_frame)
      {
        av_buffersrc_add_frame_flags(buffersrc_ctx, NULL, AV_BUFFERSRC_FLAG_KEEP_REF);
      }
      else // if not decoding in progress -> EOF
      {
        /* pull filtered frames from the filtergraph */
        ret = av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
        if (ret < 0)
          throw ffmpegException("Error occurred while sending a frame to the filter graph: %s", av_err2str(ret));
      }

      // check all the output buffers for all the outputs
      while (!killnow && ret >= 0)
      {
        for (auto out = outputs.begin(); out != outputs.end(); ++out)
        {
          int ret = out->second.filter->processFrame();
          if (!killnow && ret < 0)
            throw ffmpegException("Error occurred while retrieving a filtered frame: %s", av_err2str(ret));
        }
      }
      
      // re-lock the mutex for the status management
      thread_guard.lock();

      if (status == PAUSE_RQ)
      { // if pause requested -> destroy filter
        destroy();
        reconfigure = true;
        status = IDLE;
      }
      else if (eof)
      { // EOF -> all done
        status = IDLE;
      }
    }
  }
  catch (...)
  {
    av_log(NULL, AV_LOG_FATAL, "read_packet() thread threw exception.\n");

    // log the exception
    eptr = std::current_exception();

    // flag the exception
    std::unique_lock<std::mutex> thread_guard(thread_lock);
    killnow = true;
    status = FAILED;
    thread_ready.notify_all();
  }
}
