#include "ffmpegFilterGraph.h"

#ifndef FG_TIMEOUT_IN_MS
using namespace std::chrono_literals;
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
void Graph::child_thread_fcn(SourceBase *src)
{
  // notify the thread_fcn the arrival of AVFrame from the source buffer via inputs_ready map
  std::unique_lock<std::mutex> l(inmon_m);
  while (inmon_status >= 0) // terminate if flag is negative
  {
    if (inmon_status > 0) // monitor its input buffer
    {
      l.unlock();
      if (src->blockTillFrameReady(FG_TIMEOUT_IN_MS))
      {
        // new frame available, notify all threads
        l.lock();
        inmon_status = 1;
        inmon_cv.notify_all();
      }
      else // timed-out
        l.lock();
    }
    else // turn off monitoring, wait until requested
      inmon_cv.wait(l);
  }
}

void Graph::thread_fcn()
{
  bool reconfigure = true;

  // * create child threads to monitor source buffers
  inmon_status = 0; // initially do not run the input monitor
  std::vector<std::thread> child_threads;
  child_threads.reserve(inputs.size());
  for (auto in = inputs.begin(); in != inputs.end(); ++in) // start all child threads
    child_threads.push_back(std::thread(&Graph::child_thread_fcn, this, in->second.filter));

  // "true" if sink available
  int eof_count;

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
        eof_count = 0;
      }

      // grab at least one frame from input buffers
      bool new_frame = false;
      while (!(new_frame || killnow))
      {
        for (auto src = inputs.begin(); src != inputs.end(); ++src)
        {
          // process an incoming frame (if any)
          int ret = src->second.filter->processFrame(); // calls av_buffersrc_add_frame_flags() if frame avail
          if (ret == 0)                         // if success, set the flag to indicate the arrival
            new_frame = true;
          else if (ret != AVERROR(EAGAIN))
            throw ffmpegException("Failed to process a filter graph input AVFrame.");
        }

        // if no frame arrived, wait till there is one available
        if (!new_frame)
        {
          std::unique_lock<std::mutex> l(inmon_m);
          inmon_status = 1;
          inmon_cv.notify_all();
          inmon_cv.wait(l);
        }
      }

      // process all the output buffers
      new_frame = false;
      do
      {
        for (auto out = outputs.begin(); out != outputs.end(); ++out)
        {
          SinkBase *sink = out->second.filter;
          if (sink->enabled())
          {
            int ret = sink->processFrame(); // calls av_buffersrc_add_frame_flags() if frame avail
            if (ret == 0)                   // if success, set the flag to indicate the arrival
            {
              new_frame = true;
              if (!sink->enabled())
                ++eof_count; // check if received EOF
            }
            else if (ret != AVERROR(EAGAIN))
              throw ffmpegException("Failed to process a filter graph input AVFrame.");
          }
        }
      } while (!(new_frame || killnow || eof_count < outputs.size()));

      // re-lock the mutex for the status management
      thread_guard.lock();

      if (status == PAUSE_RQ || eof_count < outputs.size())
      { // if pause requested or all outputs closed -> destroy filter
        destroy();
        reconfigure = true;
        status = IDLE;
      }
    }
  }
  catch (...)
  {
    av_log(NULL, AV_LOG_FATAL, "[filter::Graph] Thread threw an exception.\n");

    // log the exception
    eptr = std::current_exception();

    // flag the exception
    std::unique_lock<std::mutex> thread_guard(thread_lock);
    killnow = true;
    status = FAILED;
    thread_ready.notify_all();
  }

  // terminate the child threads
  std::unique_lock<std::mutex> inmon_guard(inmon_m);
  inmon_status = -1;
  inmon_guard.unlock();
  for (auto pth = child_threads.begin(); pth != child_threads.end(); ++pth)
    if (pth->joinable())
      pth->join();
}
