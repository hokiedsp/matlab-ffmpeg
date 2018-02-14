#include "ffmpegFilterGraph.h"
#include "ffmpegFilterNullBuffers.h"

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

Graph::Graph(const std::string &filtdesc) : graph(NULL), inmon_status(0)
{
  if (filtdesc.size())
    parse(filtdesc);
}
Graph::~Graph()
{
av_log(NULL,AV_LOG_INFO,"destroying Graph\n");
      destroy(true);
av_log(NULL,AV_LOG_INFO,"destroyed Graph\n");
    }

void Graph::destroy(const bool complete)
{
  for (auto p = inputs.begin(); p != inputs.end(); ++p)
  {
    p->second.other = NULL;
    if (p->second.filter)
    {
      if (complete)
      {
        delete p->second.filter;
        p->second.filter = NULL;
        p->second.buf = NULL;
      }
      else
        p->second.filter->destroy();
    }
  }
  for (auto p = outputs.begin(); p != outputs.end(); ++p)
  {
    p->second.other = NULL;
    if (p->second.filter)
    {
      if (complete)
      {
        delete p->second.filter;
        p->second.filter = NULL;
        p->second.buf = NULL;
      }
      else
        p->second.filter->destroy();
    }
  }

  if (graph)
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
  try
  {
    if ((avfilter_graph_parse2(temp_graph, new_desc.c_str(), &ins, &outs)) < 0)
    {
      avfilter_graph_free(&temp_graph);
      throw ffmpegException("Failed to parse the filter graph description.");
    }

    // check sources to be either simple or least 1 input named
    if (ins && ins->next) // if filtergraph has more than 1 input port
    {
      bool named = false; // name index
      for (AVFilterInOut *cur = ins->next; cur; cur = cur->next)
        if (cur->name)
          named = true;
      if (!named)
        throw ffmpegException("At least one input of a complex filter graph must be named.");
    }

    // check sinks to be either simple or least 1 ouput named
    if (outs && outs->next) // if filtergraph has more than 1 input port
    {
      bool named = false; // name index
      for (AVFilterInOut *cur = outs->next; cur; cur = cur->next)
        if (cur->name)
          named = true;
      if (!named)
        throw ffmpegException("At least one output of a complex filter graph must be named.");
    }

    // all good to go!!

    // clear the existing filtergraph
    destroy(true); // destroy AVFilterContext as well as the ffmpeg::filter::* objects

    // store the filter graph
    graph = temp_graph;
    graph_desc = new_desc;

    // create source filter placeholder and mark its type
    if (ins)
      parse_sources(ins);

    // create sink filter placeholder and mark its type
    if (outs)
      parse_sinks(outs);
  }
  catch (...)
  {
    avfilter_inout_free(&ins);
    avfilter_inout_free(&outs);
    throw;
  }
  avfilter_inout_free(&ins);
  avfilter_inout_free(&outs);

  av_log(NULL, AV_LOG_ERROR, "[parse] done parsing");
}

void Graph::parse_sources(AVFilterInOut *ins)
{
  if (ins->next) // complex graph
  {
    NullVideoSource nullsrc(*this);
    for (AVFilterInOut *cur = ins; cur; cur = cur->next)
    {
      if (cur->name) // if named, add to the inputs map
        inputs[cur->name] = {avfilter_pad_get_type(cur->filter_ctx->input_pads, cur->pad_idx), NULL, NULL, cur->filter_ctx, cur->pad_idx};
      else // use nullsrc
      {
        nullsrc.configure();
        nullsrc.link(cur->filter_ctx, cur->pad_idx, 0, true);
        nullsrc.destroy(); // not really... just detaches AVFilterContext from nullsrc object
      }
    }
  }
  else // simple graph without input name
    inputs[(ins->name) ? ins->name : "in"] =
        {avfilter_pad_get_type(ins->filter_ctx->input_pads, ins->pad_idx), NULL, NULL, ins->filter_ctx, ins->pad_idx};
}

void Graph::parse_sinks(AVFilterInOut *outs)
{
  if (outs->next) // complex graph
  {
    NullVideoSink nullsink(*this);
    for (AVFilterInOut *cur = outs; cur; cur = cur->next)
    {
      if (cur->name) // if named, add to the inputs map
        outputs[cur->name] = {avfilter_pad_get_type(cur->filter_ctx->output_pads, cur->pad_idx), NULL, NULL, cur->filter_ctx, cur->pad_idx};
      else // use nullsrc
      {
        nullsink.configure();
        nullsink.link(cur->filter_ctx, cur->pad_idx, 0, false);
        nullsink.destroy(); // not really... just detaches AVFilterContext from nullsink object
      }
    }
  }
  else // simple graph
    outputs[(outs->name) ? outs->name : "out"] =
        {avfilter_pad_get_type(outs->filter_ctx->output_pads, outs->pad_idx), NULL, NULL, outs->filter_ctx, outs->pad_idx};
}

SourceBase &Graph::assignSource(IAVFrameSource &buf, const std::string &name)
{
  SourceInfo &node = inputs.at(name); // throws exception if doesn't exist
  Graph::assign_endpoint<SourceBase, VideoSource, AudioSource>(node.filter, node.type, buf);
  node.buf = &buf;
  return *node.filter;
}

SinkBase &Graph::assignSink(IAVFrameSink &buf, const std::string &name)
{
  SinkInfo &node = outputs.at(name); // throws exception if doesn't exist
  Graph::assign_endpoint<SinkBase, VideoSink, AudioSink>(node.filter, node.type, buf);
  node.buf = &buf;
  return *node.filter;
}

bool Graph::ready()
{
  // AVFilterGraph must been defined and output buffer must be non-empty
  if (!graph || outputs.empty())
  {
    av_log(NULL, AV_LOG_ERROR, "[ffmpage::filter::Graph::ready] AVFilterGraph not allocated or filter has no output\n");
    return false;
  }

  // every input & output buffers must have IAVFrameSource/IAVFrameSink associated with it
  for (auto it = inputs.begin(); it != inputs.end(); ++it)
    if (!(it->second.buf && it->second.buf->ready()))
    {
      av_log(NULL, AV_LOG_ERROR, "[ffmpage::filter::Graph::ready] Input '%s' is not ready\n", it->first.c_str());
      return false;
    }
  for (auto it = outputs.begin(); it != outputs.end(); ++it)
    if (!(it->second.buf && it->second.buf->ready()))
    {
      av_log(NULL, AV_LOG_ERROR, "[ffmpage::filter::Graph::ready] Output '%s' is not ready\n", it->first.c_str());
      return false;
    }

  return true;
}

void Graph::flush()
{
  if (!graph)
    throw ffmpegException("[ffmpeg::filter::Graph::flush] No filter graph to flush.");

  // create the new graph (automatically destroys the previous one)
  // allocate new filter graph
  AVFilterGraph *temp_graph;
  if (!(temp_graph = avfilter_graph_alloc()))
    throw ffmpegException(AVERROR(ENOMEM));
  graph = temp_graph;

  // Parse the string to get I/O endpoints
  AVFilterInOut *ins = NULL, *outs = NULL;
  avfilter_graph_parse2(graph, graph_desc.c_str(), &ins, &outs);

  // assign sources
  AVFilterInOut *cur = ins;
  if (inputs.size() == 1) // single-input
  {
    SourceInfo &node = inputs.at(cur->name ? cur->name : "in"); // throws exception if doesn't exist
    if (!node.buf)
      throw ffmpegException("[ffmpeg::filter::Graph::flush] Filter graph does not have a source buffer.");
    Graph::assign_endpoint<SourceBase, VideoSource, AudioSource>(node.filter, node.type, *node.buf);
    node.other = cur->filter_ctx;
    cur = cur->next;
  }
  NullVideoSource nullsrc(*this);
  for (; cur; cur = cur->next)
  {
    if (cur->name) // if named, add to the inputs map
    {
      SourceInfo &node = inputs.at(cur->name ? cur->name : "in"); // throws exception if doesn't exist
      if (!node.buf)
        throw ffmpegException("[ffmpeg::filter::Graph::flush] Filter graph does not have a source buffer.");
      Graph::assign_endpoint<SourceBase, VideoSource, AudioSource>(node.filter, node.type, *node.buf);
      node.other = cur->filter_ctx;
    }
    else // use nullsrc
    {
      nullsrc.configure();
      nullsrc.link(cur->filter_ctx, cur->pad_idx, 0, true);
      nullsrc.destroy(); // not really... just detaches AVFilterContext from nullsrc object
    }
  }

  // assign sinks
  cur = outs;
  if (outputs.size() == 1) // single-output
  {
    SinkInfo &node = outputs.at(cur->name ? cur->name : "out"); // throws exception if doesn't exist
    if (!node.buf)
      throw ffmpegException("[ffmpeg::filter::Graph::flush] Filter graph does not have a sink buffer.");
    Graph::assign_endpoint<SinkBase, VideoSink, AudioSink>(node.filter, node.type, *node.buf);
    node.other = cur->filter_ctx;
    cur = cur->next;
  }
  NullVideoSink nullsink(*this);
  for (AVFilterInOut *cur = outs; cur; cur = cur->next)
  {
    if (cur->name) // if named, add to the inputs map
    {
      SinkInfo &node = outputs.at(cur->name ? cur->name : "out"); // throws exception if doesn't exist
      if (!node.buf)
        throw ffmpegException("[ffmpeg::filter::Graph::flush] Filter graph does not have a sink buffer.");
      Graph::assign_endpoint<SinkBase, VideoSink, AudioSink>(node.filter, node.type, *node.buf);
      node.other = cur->filter_ctx;
      cur = cur->next;
    }
    else // use nullsrc
    {
      nullsink.configure();
      nullsink.link(cur->filter_ctx, cur->pad_idx, 0, false);
      nullsink.destroy(); // not really... just detaches AVFilterContext from nullsink object
    }
  }

  // finalize the graph
  configure();
}

void Graph::configure()
{
  // configure source filters
  for (auto in = inputs.begin(); in != inputs.end(); ++in)
  {
    SourceBase *src = in->second.filter;
    if (!src) // also check for buffer states
      throw ffmpegException("[ffmpeg::filter::Graph::configure] Source filter is not set.");

    // load media parameters from buffer
    if (!src->updateMediaParameters())
      throw ffmpegException("[ffmpeg::filter::Graph::configure] Source buffer does not have all the necessary media parameters to configure source filter.");

    // configure filter (constructs the filter context)
    src->configure(in->first);

    // link filter
    src->link(in->second.other, in->second.otherpad);
  }

  // configure sink filters
  for (auto out = outputs.begin(); out != outputs.end(); ++out)
  {
    SinkBase *sink = out->second.filter;
    if (!sink)
      throw ffmpegException("[ffmpeg::filter::Graph::configure] Sink filter is not set.");

    // configure filter
    sink->configure(out->first);

    // link the sink to the last filter
    sink->link(out->second.other, out->second.otherpad);
  }
  // finalize the graph
  if (avfilter_graph_config(graph, NULL) < 0)
    throw ffmpegException("[ffmpeg::filter::Graph::configure] Failed to finalize the filter graph.");

  // fg->reconfiguration = 1;
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

void Graph::runOnce(const std::chrono::milliseconds &rel_time)
{
  // grab one frame from at least one input buffers
  bool new_frame = false;
  for (auto src = inputs.begin(); src != inputs.end(); ++src)
  {
    // process an incoming frame (if any)
    int ret = src->second.filter->processFrame(); // calls av_buffersrc_add_frame_flags() if frame avail
    if (ret == 0)                                 // if success, set the flag to indicate the arrival
      new_frame = true;
    else if (ret != AVERROR(EAGAIN))
      throw ffmpegException("[ffmpeg::filter::Graph::runOnce] Failed to process a filter graph input AVFrame.");
  }

  // if no frame arrived, error out
  if (!new_frame)
    throw ffmpegException("[ffmpeg::filter::Graph::runOnce] No data were available to the filter graph.");

  // process all the output buffers
  new_frame = false;
  int eof_count = 0;
  do
  {
    for (auto out = outputs.begin(); out != outputs.end(); ++out)
    {
      SinkBase *sink = out->second.filter;
      int ret = sink->processFrame(rel_time); // calls av_buffersrc_add_frame_flags() if frame avail
      if (ret == 0)                   // if success, set the flag to indicate the arrival
        new_frame = true;
      else if (ret == AVERROR_EOF)
        ++eof_count; // check if received EOF
      else if (ret != AVERROR(EAGAIN))
        throw ffmpegException("[ffmpeg::filter::Graph::runOnce] Failed to process a filter graph output AVFrame.");
    }
  } while (!(new_frame || eof_count < outputs.size()));
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
          if (ret == 0)                                 // if success, set the flag to indicate the arrival
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
