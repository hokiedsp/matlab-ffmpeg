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
  av_log(NULL, AV_LOG_INFO, "destroying Graph\n");
  destroy(true);
  av_log(NULL, AV_LOG_INFO, "destroyed Graph\n");
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
        p->second.buf = NULL;
      }
      else
        p->second.filter->purge();
    }
    p->second.conns.clear();
  }
  for (auto p = outputs.begin(); p != outputs.end(); ++p)
  {
    if (p->second.filter)
    {
      if (complete)
      {
        delete p->second.filter;
        p->second.filter = NULL;
        p->second.buf = NULL;
      }
      else
        p->second.filter->purge();
    }
    p->second.conns.clear();
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

  // set unique_ptrs to auto delete the pointer when going out of scope
  auto avFilterGraphFree = [](AVFilterGraph *graph) { avfilter_graph_free(&graph); };
  std::unique_ptr<AVFilterGraph, decltype(avFilterGraphFree)> ptemp(temp_graph, avFilterGraphFree);

  // Parse the string to get I/O endpoints
  AVFilterInOut *ins = NULL, *outs = NULL;
  if ((avfilter_graph_parse2(temp_graph, new_desc.c_str(), &ins, &outs)) < 0)
    throw ffmpegException("Failed to parse the filter graph description.");

  // set unique_ptrs to auto delete the pointer when going out of scope
  auto avFilterInOutFree = [](AVFilterInOut *ptr) { avfilter_inout_free(&ptr); };
  std::unique_ptr<AVFilterInOut, decltype(avFilterInOutFree)> pin(ins, avFilterInOutFree), pout(outs, avFilterInOutFree);

  // check sources to be either simple or least 1 input named
  if (ins && ins->next) // if filtergraph has more than 1 input port
  {
    bool named = false; // name index
    for (AVFilterInOut *cur = ins->next; cur; cur = cur->next)
      if (cur->name)
        named = true;
    if (!named)
      throw ffmpegException("All the inputs of multiple-input complex filter graph must be named.");
  }

  // check sinks to be either simple or least 1 ouput named
  if (outs && outs->next) // if filtergraph has more than 1 input port
  {
    bool named = false; // name index
    for (AVFilterInOut *cur = outs->next; cur; cur = cur->next)
      if (cur->name)
        named = true;
    if (!named)
      throw ffmpegException("All the outputs of multiple-output complex filter graph must be named.");
  }

  // all good to go!!

  // clear the existing filtergraph
  destroy(true); // destroy AVFilterContext as well as the ffmpeg::filter::* objects

  // store the filter graph
  ptemp.release(); // now keeping the graph, so release it from the unique_ptr
  graph = temp_graph;
  graph_desc = new_desc;

  // create source filter placeholder and mark its type
  if (ins)
    parse_sources(ins);

  // create sink filter placeholder and mark its type
  if (outs)
    parse_sinks(outs);

  av_log(NULL, AV_LOG_ERROR, "[parse] done parsing");
}

void Graph::parse_sources(AVFilterInOut *ins)
{
  if (ins->next) // complex graph
  {
    std::unique_ptr<NullVideoSource> nullsrc;
    for (AVFilterInOut *cur = ins; cur; cur = cur->next)
    {
      if (cur->name) // if named, add to the inputs map
      {
        auto search = inputs.find(cur->name);
        if (search != inputs.end())
          search->second.conns.push_back(ConnectedTo({cur->filter_ctx, cur->pad_idx}));
        else
          search->second =
              {avfilter_pad_get_type(cur->filter_ctx->input_pads, cur->pad_idx),
               NULL,
               NULL,
               ConnectionList(1,ConnectedTo({cur->filter_ctx, cur->pad_idx}))};
      }
      else // use nullsrc
        connect_nullsource(cur);
    }
  }
  else // simple graph without input name
    inputs[(ins->name) ? ins->name : "in"] = {
        avfilter_pad_get_type(ins->filter_ctx->input_pads, ins->pad_idx),
        NULL,
        NULL,
        ConnectionList(1,ConnectedTo({ins->filter_ctx, ins->pad_idx}))};
}
  void Graph::connect_nullsource(AVFilterInOut *in)
  {
    const AVFilter *filter;
    AVFilterContext *context;

    if (avfilter_pad_get_type(in->filter_ctx->input_pads, in->pad_idx) == AVMEDIA_TYPE_VIDEO)
      filter = avfilter_get_by_name("nullsrc");
    else
      filter = avfilter_get_by_name("anullsrc");

    if (avfilter_graph_create_filter(&context, filter, "", "", NULL, graph) < 0)
      throw ffmpegException("[ffmpeg::filter::Graph::connect_nullsink] Failed to create a null source.");
    if (avfilter_link(context, 0, in->filter_ctx, in->pad_idx) < 0)
      throw ffmpegException("[ffmpeg::filter::Graph::connect_nullsink] Failed to link null source to the filter graph.");
  }

void Graph::parse_sinks(AVFilterInOut *outs)
{
  if (outs->next) // complex graph
  {
    std::unique_ptr<NullVideoSink> nullsink;
    for (AVFilterInOut *cur = outs; cur; cur = cur->next)
    {
      if (cur->name) // if named, add to the inputs map
      {
        auto search = outputs.find(cur->name);
        if (search != outputs.end())
          search->second.conns.push_back(ConnectedTo({cur->filter_ctx, cur->pad_idx}));
        else
          search->second =
              {avfilter_pad_get_type(cur->filter_ctx->output_pads, cur->pad_idx),
               NULL,
               NULL,
               ConnectionList(1,ConnectedTo({cur->filter_ctx, cur->pad_idx}))};
      }
      else // use nullsink
        connect_nullsink(cur);
    }
  }
  else // simple graph
    outputs[(outs->name) ? outs->name : "out"] = {
        avfilter_pad_get_type(outs->filter_ctx->output_pads, outs->pad_idx),
        NULL,
        NULL,
        ConnectionList(1,ConnectedTo({outs->filter_ctx, outs->pad_idx}))};
}

void Graph::connect_nullsink(AVFilterInOut *out)
{
  const AVFilter *filter;
  AVFilterContext *context;

  if (avfilter_pad_get_type(out->filter_ctx->output_pads, out->pad_idx) == AVMEDIA_TYPE_VIDEO)
    filter = avfilter_get_by_name("nullsink");
  else
    filter = avfilter_get_by_name("anullsink");

  if (avfilter_graph_create_filter(&context, filter, "", "", NULL, graph) < 0)
    throw ffmpegException("[ffmpeg::filter::Graph::connect_nullsource] Failed to create a null sink.");
  if (avfilter_link(out->filter_ctx, out->pad_idx, context, 0) < 0)
    throw ffmpegException("[ffmpeg::filter::Graph::connect_nullsource] Failed to link null sink to the filter graph.");
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
    if (!it->second.buf)
    {
      av_log(NULL, AV_LOG_ERROR, "[ffmpage::filter::Graph::ready] No buffer assigned to Input '%s'\n", it->first.c_str());
      return false;
    }
  for (auto it = outputs.begin(); it != outputs.end(); ++it)
    if (!it->second.buf)
    {
      av_log(NULL, AV_LOG_ERROR, "[ffmpage::filter::Graph::ready] No buffer assigned to Output '%s'\n", it->first.c_str());
      return false;
    }

  return true;
}

void Graph::flush()
{
  if (!graph)
    throw ffmpegException("[ffmpeg::filter::Graph::flush] No filter graph to flush.");

  // destroy the existing filter graph
  destroy();

  // create the new graph
  // allocate new filter graph
  AVFilterGraph *temp_graph;
  if (!(temp_graph = avfilter_graph_alloc()))
    throw ffmpegException(AVERROR(ENOMEM));
  graph = temp_graph;

  // Parse the string to get I/O endpoints
  AVFilterInOut *ins = NULL, *outs = NULL;
  avfilter_graph_parse2(graph, graph_desc.c_str(), &ins, &outs);

  // set unique_ptrs to auto delete the pointer when going out of scope
  auto avFilterInOutFree = [](AVFilterInOut *ptr) { avfilter_inout_free(&ptr); };
  std::unique_ptr<AVFilterInOut, decltype(avFilterInOutFree)> pin(ins, avFilterInOutFree), pout(outs, avFilterInOutFree);

  // assign sources
  if (inputs.size() == 1) // single-input
  {
    SourceInfo &node = inputs.at(ins->name ? ins->name : "in"); // nameless=>auto-name as "in"
    node.conns.push_back(ConnectedTo({ins->filter_ctx, ins->pad_idx}));
  }
  else // multiple input
  {
    for (AVFilterInOut *cur = ins; cur; cur = cur->next)
    {
      if (cur->name) // if named, add to the inputs map
      {
        SourceInfo &node = inputs.at(cur->name); // throws exception if doesn't exist
        node.conns.push_back(ConnectedTo({cur->filter_ctx, cur->pad_idx}));
      }
      else // use nullsrc
        connect_nullsource(cur);
    }
  }

  // assign sinks
  if (outputs.size() == 1) // single-output
  {
    SinkInfo &node = outputs.at(outs->name ? outs->name : "out"); // if not named, auto-name as "out"
    if (!node.buf)
      throw ffmpegException("[ffmpeg::filter::Graph::flush] Filter graph does not have a sink buffer.");
    node.conns.push_back(ConnectedTo({outs->filter_ctx, outs->pad_idx}));
  }
  else // multiple-output
  {
    NullVideoSink nullsink(*this);
    for (AVFilterInOut *cur = outs; cur; cur = cur->next)
    {
      if (cur->name) // if named, add to the inputs map
      {
        SinkInfo &node = outputs.at(cur->name); // throws exception if doesn't exist
        if (!node.buf)
          throw ffmpegException("[ffmpeg::filter::Graph::flush] Filter graph does not have a sink buffer.");
        node.conns.push_back(ConnectedTo({cur->filter_ctx, cur->pad_idx}));
      }
      else // use nullsink
        connect_nullsink(cur);
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
    for (auto conn = in->second.conns.begin(); conn < in->second.conns.end(); ++conn)
      src->link(conn->other, conn->otherpad);
    in->second.conns.clear();
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
    for (auto conn = out->second.conns.begin(); conn < out->second.conns.end(); ++conn)
      sink->link(conn->other, conn->otherpad);
    out->second.conns.clear();
  }

  // finalize the graph
  if (avfilter_graph_config(graph, NULL) < 0)
    throw ffmpegException("[ffmpeg::filter::Graph::configure] Failed to finalize the filter graph.");
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
      int ret = sink->processFrame(rel_time); // calls av_buffersink_get_frame() if frame avail
      if (ret == 0)                           // if success, set the flag to indicate the arrival
        new_frame = true;
      else if (ret == AVERROR_EOF)
        ++eof_count; // check if received EOF
      else if (ret != AVERROR(EAGAIN))
        throw ffmpegException("[ffmpeg::filter::Graph::runOnce] Failed to process a filter graph output AVFrame.");
    }
  } while (!(new_frame || eof_count < outputs.size()));
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
