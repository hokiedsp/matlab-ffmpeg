#include "ffmpegFilterGraph.h"

#include <algorithm>

#ifndef FG_TIMEOUT_IN_MS
#define FG_TIMEOUT_IN_MS 100ms
#endif

// #include <stdint.h>

extern "C"
{
  // #include "libavfilter/avfilter.h"
  // #include "libavresample/avresample.h"
  // #include "libavutil/avassert.h"
}

#include <algorithm>

using namespace ffmpeg;
using namespace filter;

Graph::Graph(const std::string &filtdesc) : graph(NULL), inmon_status(0)
{
  if (filtdesc.size()) parse(filtdesc);
}
Graph::~Graph() { clear(); }

void Graph::clear()
{
  // if no filtergraph has been created, nothing to do
  if (!graph) return;

  // destroy the AVFilterGraph object and clear associated description string
  avfilter_graph_free(&graph);
  graph_desc.clear();

  // traverse inputs and delete ffmpeg::filter objects
  for (auto p = inputs.begin(); p != inputs.end(); ++p)
  {
    if (p->second.filter) // if ffmpeg::filter object has been set
    {
      p->second.filter->purge(); // must purge first because AVFilterContext has
                                 // already been freed
      delete p->second.filter;
    }
  }

  // traverse outputs and delete ffmpeg::filter objects
  for (auto p = outputs.begin(); p != outputs.end(); ++p)
  {
    if (p->second.filter)
    {
      p->second.filter->purge(); // must purge first because AVFilterContext has
                                 // already been freed
      delete p->second.filter;
    }
  }

  // clear the input and output maps
  inputs.clear();
  outputs.clear();
}

void Graph::purge()
{
  // if no filtergraph has been created, nothing to do
  if (!graph) return;

  // destroy the AVFilterGraph object (but keep the associated description
  // string)
  avfilter_graph_free(&graph);

  // traverse inputs and purge AVFilterContext pointers
  for (auto p = inputs.begin(); p != inputs.end(); ++p)
  {
    if (p->second.filter) // if ffmpeg::filter object has been set
      p->second.filter->purge();
  }

  // traverse outputs and purge AVFilterContext pointers
  for (auto p = outputs.begin(); p != outputs.end(); ++p)
  {
    if (p->second.filter) p->second.filter->purge();
  }
}

void Graph::parse(const std::string &new_desc)
{
  // allocate new filter graph
  AVFilterGraph *temp_graph;
  if (!(temp_graph = avfilter_graph_alloc())) throw Exception(AVERROR(ENOMEM));

  // set unique_ptrs to auto delete the pointer when going out of scope
  auto avFilterGraphFree = [](AVFilterGraph *graph) {
    avfilter_graph_free(&graph);
  };
  std::unique_ptr<AVFilterGraph, decltype(avFilterGraphFree)> ptemp(
      temp_graph, avFilterGraphFree);

  // Parse the string to get I/O endpoints
  AVFilterInOut *ins = NULL, *outs = NULL;
  if ((avfilter_graph_parse2(temp_graph, new_desc.c_str(), &ins, &outs)) < 0)
    throw Exception("Failed to parse the filter graph description.");

  // set unique_ptrs to auto delete the pointer when going out of scope
  auto avFilterInOutFree = [](AVFilterInOut *ptr) {
    avfilter_inout_free(&ptr);
  };
  std::unique_ptr<AVFilterInOut, decltype(avFilterInOutFree)> pin(
      ins, avFilterInOutFree),
      pout(outs, avFilterInOutFree);

  // check sources to be either simple or least 1 input named
  if (ins && ins->next) // if filtergraph has more than 1 input port
  {
    bool named = false; // name index
    for (AVFilterInOut *cur = ins->next; cur; cur = cur->next)
      if (cur->name) named = true;
    if (!named)
      throw Exception("All the inputs of multiple-input complex filter "
                      "graph must be named.");
  }

  // check sinks to be either simple or least 1 ouput named
  if (outs && outs->next) // if filtergraph has more than 1 input port
  {
    bool named = false; // name index
    for (AVFilterInOut *cur = outs->next; cur; cur = cur->next)
      if (cur->name) named = true;
    if (!named)
      throw Exception("All the outputs of multiple-output complex filter "
                      "graph must be named.");
  }

  // clear the existing filtergraph
  clear(); // destroy AVFilterContext as well as the ffmpeg::filter::* objects

  // store the filter graph
  ptemp.release(); // now keeping the graph, so release it from the unique_ptr
  graph = temp_graph;
  graph_desc = new_desc;

  // create source filter placeholder and mark its type
  if (ins) parse_sources(ins);

  // create sink filter placeholder and mark its type
  if (outs) parse_sinks(outs);
}

void Graph::parse_sources(AVFilterInOut *ins)
{
  if (ins->next) // multi-input graph
  {
    // check for # of unnamed inputs
    int vmulti = -1, amulti = -1;
    for (AVFilterInOut *cur = ins; cur && vmulti < 1 && amulti < 1;
         cur = cur->next)
    {
      if (cur->name) continue; // if named, add to the inputs map
      auto type =
          avfilter_pad_get_type(cur->filter_ctx->input_pads, cur->pad_idx);
      if (vmulti < 1 && type == AVMEDIA_TYPE_VIDEO)
        ++vmulti;
      else if (amulti < 1 && type == AVMEDIA_TYPE_AUDIO)
        ++amulti;
    }

    // parse the input connection info
    int vcnt = 0, acnt = 0;
    std::string vpre = (acnt < 0) ? "in" : "inv";
    std::string apre = (vcnt < 0) ? "in" : "ina";

    // if default name already taken, append counter
    if (vmulti == 0 && inputs.count(vpre)) ++vmulti;
    if (amulti == 0 && inputs.count(apre)) ++amulti;

    for (AVFilterInOut *cur = ins; cur; cur = cur->next)
    {
      // pick the input pad name
      std::string name;
      if (cur->name)             // given
      { name = cur->name; } else // unnamed -> assign a unique name
      {
        auto type =
            avfilter_pad_get_type(cur->filter_ctx->input_pads, cur->pad_idx);
        if (type == AVMEDIA_TYPE_VIDEO)
        {
          name = vpre;
          if (vmulti > 0)
          {
            do
            {
              name = vpre + std::to_string(++vcnt);
            } while (inputs.count(name));
          }
        }
        else if (type == AVMEDIA_TYPE_AUDIO)
        {
          do
          {
            name = vpre + std::to_string(++acnt);
          } while (inputs.count(name));
        }
      }

      auto search = inputs.find(name);
      if (search != inputs.end())
        search->second.conns.push_back(
            ConnectTo({cur->filter_ctx, cur->pad_idx}));
      else
        inputs[name] = {
            avfilter_pad_get_type(cur->filter_ctx->input_pads, cur->pad_idx),
            -1,
            -1,
            NULL,
            NULL,
            ConnectionList(1, ConnectTo({cur->filter_ctx, cur->pad_idx}))};
    }
  }
  else // single-input graph
    inputs[(ins->name) ? ins->name : "in"] = {
        avfilter_pad_get_type(ins->filter_ctx->input_pads, ins->pad_idx),
        -1,
        -1,
        NULL,
        NULL,
        ConnectionList(1, ConnectTo({ins->filter_ctx, ins->pad_idx}))};
}
void Graph::connect_nullsources()
{
  for (auto in = inputs.begin(); in != inputs.end(); ++in)
  {
    auto &in_info = in->second;
    if (in_info.buf) continue; // connected to a source buffer

    const AVFilter *filter;
    if (in_info.type == AVMEDIA_TYPE_VIDEO)
      filter = avfilter_get_by_name("nullsrc");
    else
      filter = avfilter_get_by_name("anullsrc");

    AVFilterContext *context;
    if (avfilter_graph_create_filter(&context, filter, "", "", NULL, graph) < 0)
      throw Exception("[ffmpeg::filter::Graph::connect_nullsink] Failed "
                      "to create a null source.");

    auto conn = in_info.conns.front();
    if (avfilter_link(context, 0, conn.other, conn.otherpad) < 0)
      throw Exception("[ffmpeg::filter::Graph::connect_nullsink] Failed "
                      "to link null source to the filter graph.");
  }
}

void Graph::parse_sinks(AVFilterInOut *outs)
{
  if (outs->next) // multiple-output graph
  {
    // check for # of unnamed inputs
    int vmulti = -1, amulti = -1;
    for (AVFilterInOut *cur = outs; cur; cur = cur->next)
    {
      if (cur->name) continue; // if named, add to the inputs map
      auto type =
          avfilter_pad_get_type(cur->filter_ctx->output_pads, cur->pad_idx);
      if (vmulti < 1 && type == AVMEDIA_TYPE_VIDEO)
        ++vmulti;
      else if (amulti < 1 && type == AVMEDIA_TYPE_AUDIO)
        ++amulti;
    }

    // parse the input connection info
    int vcnt = 0, acnt = 0;
    std::string vpre = (acnt < 0) ? "out" : "outv";
    std::string apre = (vcnt < 0) ? "out" : "outa";

    // if default name already taken, append counter
    if (vmulti == 0 && outputs.count(vpre)) ++vmulti;
    if (amulti == 0 && outputs.count(apre)) ++amulti;

    for (AVFilterInOut *cur = outs; cur; cur = cur->next)
    {
      // pick the input pad name
      std::string name;
      if (cur->name)             // given
      { name = cur->name; } else // unnamed -> assign a unique name
      {
        auto type =
            avfilter_pad_get_type(cur->filter_ctx->output_pads, cur->pad_idx);
        if (type == AVMEDIA_TYPE_VIDEO)
        {
          name = vpre;
          if (vmulti > 0)
          {
            do
            {
              name = vpre + std::to_string(++vcnt);
            } while (outputs.count(name));
          }
        }
        else if (type == AVMEDIA_TYPE_AUDIO)
        {
          do
          {
            name = vpre + std::to_string(++acnt);
          } while (inputs.count(name));
        }
      }

      outputs[cur->name] = {
          avfilter_pad_get_type(cur->filter_ctx->output_pads, cur->pad_idx),
          -1,
          -1,
          NULL,
          NULL,
          ConnectTo({cur->filter_ctx, cur->pad_idx})};
    }
  }
  else // single-output graph
    outputs[(outs->name) ? outs->name : "out"] = {
        avfilter_pad_get_type(outs->filter_ctx->output_pads, outs->pad_idx),
        -1,
        -1,
        NULL,
        NULL,
        ConnectTo({outs->filter_ctx, outs->pad_idx})};
}

void Graph::connect_nullsinks()
{
  for (auto out = outputs.begin(); out != outputs.end(); ++out)
  {
    auto &out_info = out->second;
    if (out_info.buf) continue;

    const AVFilter *filter;
    if (out_info.type == AVMEDIA_TYPE_VIDEO)
      filter = avfilter_get_by_name("nullsink");
    else
      filter = avfilter_get_by_name("anullsink");

    AVFilterContext *context;
    if (avfilter_graph_create_filter(&context, filter, "", "", NULL, graph) < 0)
      throw Exception("[ffmpeg::filter::Graph::connect_nullsource] "
                      "Failed to create a null sink.");

    auto conn = out_info.conn;
    if (avfilter_link(conn.other, conn.otherpad, context, 0) < 0)
      throw Exception("[ffmpeg::filter::Graph::connect_nullsource] "
                      "Failed to link null sink to the filter graph.");
  }
}

SourceBase &Graph::assignSource(IAVFrameSourceBuffer &buf,
                                const std::string &name)
{
  auto &node = inputs.at(name); // throws exception if doesn't exist
  Graph::assign_endpoint<SourceBase, VideoSource, AudioSource,
                         IAVFrameSourceBuffer>(node.filter, node.type, buf);
  node.buf = &buf;
  buf.setDst(*node.filter);

  return *node.filter;
}

std::string Graph::getNextUnassignedSink(const std::string &last,
                                         const AVMediaType type)
{
  bool any_media = (type == AVMEDIA_TYPE_UNKNOWN);

  auto out = last.size() ? ++outputs.find(last) : outputs.begin();
  out = std::find_if(out, outputs.end(), [any_media, type](auto &sinkinfo) {
    return (!sinkinfo.second.buf &&
            (any_media || sinkinfo.second.filter->getMediaType() == type));
  });
  return out != outputs.end() ? out->first : "";
}

SinkBase &Graph::assignSink(IAVFrameSinkBuffer &buf, const std::string &name)
{
  SinkInfo &node = outputs.at(name); // throws exception if doesn't exist
  Graph::assign_endpoint<SinkBase, VideoSink, AudioSink, IAVFrameSinkBuffer>(
      node.filter, node.type, buf);
  node.buf = &buf;
  buf.setSrc(*node.filter);
  return *node.filter;
}

bool Graph::ready()
{
  // AVFilterGraph must been defined and output buffer must be non-empty
  if (!graph || outputs.empty()) return false;

  // every input & output buffers must have
  // IAVFrameSourceBuffer/IAVFrameSinkBuffer associated with it
  for (auto it = inputs.begin(); it != inputs.end(); ++it)
    if (!it->second.buf) return false;
  for (auto it = outputs.begin(); it != outputs.end(); ++it)
    if (!it->second.buf) return false;

  return true;
}

void Graph::flush()
{
  if (!graph) return;

  // destroy the existing AVFilterGraph w/out losing the graph structure
  purge();

  // allocate new filter graph
  if (!(graph = avfilter_graph_alloc())) throw Exception(AVERROR(ENOMEM));

  // Parse the string to get I/O endpoints
  AVFilterInOut *ins = NULL, *outs = NULL;
  avfilter_graph_parse2(graph, graph_desc.c_str(), &ins, &outs);

  // set unique_ptrs to auto-delete the pointer when going out of scope
  auto avFilterInOutFree = [](AVFilterInOut *ptr) {
    avfilter_inout_free(&ptr);
  };
  std::unique_ptr<AVFilterInOut, decltype(avFilterInOutFree)> pin(
      ins, avFilterInOutFree),
      pout(outs, avFilterInOutFree);

  // assign sources
  if (ins)
  {
    if (ins->next) // multiple-inputs
    {
      std::vector<std::string> unnamed_inputs;
      for (auto in = inputs.begin(); in != inputs.end(); ++in)
        unnamed_inputs.push_back(in->first);
      for (AVFilterInOut *cur = ins; cur; cur = cur->next)
        if (cur->name)
          unnamed_inputs.erase(std::find(unnamed_inputs.begin(),
                                         unnamed_inputs.end(), cur->name));

      auto vnext = std::find_if(inputs.begin(), inputs.end(), [](auto &in) {
        return in.second.type == AVMEDIA_TYPE_VIDEO;
      });
      auto anext = std::find_if(inputs.begin(), inputs.end(), [](auto &in) {
        return in.second.type == AVMEDIA_TYPE_AUDIO;
      });

      for (AVFilterInOut *cur = ins; cur; cur = cur->next)
      {
        std::string name;

        if (cur->name) // if named, add to the inputs map
        { name = cur->name; } else
        {
          auto type =
              avfilter_pad_get_type(cur->filter_ctx->input_pads, cur->pad_idx);
          if (type == AVMEDIA_TYPE_VIDEO)
          {
            name = vnext->first;
            while ((++vnext)->second.type != AVMEDIA_TYPE_VIDEO)
              ;
          }
          else
          {
            name = anext->first;
            while ((++anext)->second.type != AVMEDIA_TYPE_AUDIO)
              ;
          }
        }

        inputs.at(name).conns.push_back(
            ConnectTo({cur->filter_ctx, cur->pad_idx}));
      }

      // connect null sources to input pads without avframe buffer
      connect_nullsources();
    }
    else // single input
    {
      SourceInfo &node = inputs.at(
          ins->name ? ins->name : "in"); // nameless=>auto-name as "in"
      node.conns.push_back(ConnectTo({ins->filter_ctx, ins->pad_idx}));
    }
  }
  // assign sinks
  if (outs)
  {
    if (outs->next) // multiple-outputs
    {
      std::vector<std::string> unnamed_outputs;
      for (auto out = outputs.begin(); out != outputs.end(); ++out)
        unnamed_outputs.push_back(out->first);
      for (AVFilterInOut *cur = ins; cur; cur = cur->next)
        if (cur->name)
          unnamed_outputs.erase(std::find(unnamed_outputs.begin(),
                                          unnamed_outputs.end(), cur->name));

      auto vnext = std::find_if(outputs.begin(), outputs.end(), [](auto &out) {
        return out.second.type == AVMEDIA_TYPE_VIDEO;
      });
      auto anext = std::find_if(outputs.begin(), outputs.end(), [](auto &out) {
        return out.second.type == AVMEDIA_TYPE_AUDIO;
      });

      for (AVFilterInOut *cur = outs; cur; cur = cur->next)
      {
        std::string name;

        if (cur->name) // if named, add to the inputs map
        { name = cur->name; } else
        {
          auto type =
              avfilter_pad_get_type(cur->filter_ctx->output_pads, cur->pad_idx);
          if (type == AVMEDIA_TYPE_VIDEO)
          {
            name = vnext->first;
            while ((++vnext)->second.type != AVMEDIA_TYPE_VIDEO)
              ;
          }
          else
          {
            name = anext->first;
            while ((++anext)->second.type != AVMEDIA_TYPE_AUDIO)
              ;
          }
        }

        outputs.at(name).conn = {cur->filter_ctx, cur->pad_idx};
      }

      // connect unbuffered output to nullsinks
      connect_nullsinks();
    }
  }
  else // single-output
  {
    SinkInfo &node = outputs.at(
        outs->name ? outs->name : "out"); // if not named, auto-name as "out"
    if (!node.buf)
      throw Exception("[ffmpeg::filter::Graph::flush] Filter graph does "
                      "not have a sink buffer.");
    node.conn = {outs->filter_ctx, outs->pad_idx};
  }

  // finalize the graph
  configure();
}

void Graph::use_src_splitter(SourceBase *src, const ConnectionList &conns)
{
  // if connects to multiple filters, we need to insert "split" filter block

  // determine the type of splitter
  const AVFilter *filter;
  if (src->getMediaType() == AVMEDIA_TYPE_VIDEO)
    filter = avfilter_get_by_name("split");
  else
    filter = avfilter_get_by_name("asplit");

  // create the splitter
  AVFilterContext *context;
  if (avfilter_graph_create_filter(&context, filter,
                                   std::to_string(conns.size()).c_str(), "",
                                   NULL, graph) < 0)
    throw Exception("[ffmpeg::filter::Graph::insert_src_splitter] Failed "
                    "to create a splitter.");

  // link splitter to the source
  src->link(context, 0);

  // link splitter to the filters that use the src buffer
  for (size_t i = 0; i < conns.size(); ++i)
    if (avfilter_link(context, (unsigned int)i, conns[i].other,
                      conns[i].otherpad) < 0)
      throw Exception("[ffmpeg::filter::Graph::insert_src_splitter] "
                      "Failed to link splitter to the filter graph.");
}

void Graph::configure()
{
  // configure source filters
  for (auto in = inputs.begin(); in != inputs.end(); ++in)
  {
    SourceBase *src = in->second.filter;
    if (!src) // also check for buffer states
      throw Exception(
          "[ffmpeg::filter::Graph::configure] Source filter is not set.");

    // configure filter (constructs the filter context)
    src->configure(in->first);

    // link filter
    if (in->second.conns.size() == 1)
      src->link(in->second.conns[0].other, in->second.conns[0].otherpad);
    else
      use_src_splitter(src, in->second.conns);

    in->second.conns.clear(); // no longer needed
  }

  // configure sink filters
  for (auto out = outputs.begin(); out != outputs.end(); ++out)
  {
    SinkBase *sink = out->second.filter;
    if (!sink)
      throw Exception(
          "[ffmpeg::filter::Graph::configure] Sink filter is not set.");

    // configure filter
    sink->configure(out->first);

    // link the sink to the last filter
    sink->link(out->second.conn.other, out->second.conn.otherpad);
  }

  // finalize the graph
  if (avfilter_graph_config(graph, NULL) < 0)
    throw Exception("[ffmpeg::filter::Graph::configure] Failed to "
                    "finalize the filter graph.");
}

// used to append filters for autorotate feature
int Graph::insert_filter(AVFilterContext *&last_filter, int &pad_idx,
                         const std::string &filter_name,
                         const std::string &args)
{
  AVFilterGraph *graph = last_filter->graph;
  AVFilterContext *ctx;
  int ret;

  ret = avfilter_graph_create_filter(
      &ctx, avfilter_get_by_name(filter_name.c_str()), filter_name.c_str(),
      args.c_str(), NULL, graph);
  if (ret < 0) return ret;

  ret = avfilter_link(last_filter, pad_idx, ctx, 0);
  if (ret < 0) return ret;

  last_filter = ctx;
  pad_idx = 0;
  return 0;
}

// parse and  sources to input streams
void Graph::parseSourceStreamSpecs(const std::vector<InputFormat *> fmts)
{
  std::vector<std::string> invalid_names;

  for (auto in = inputs.begin(); in != inputs.end(); ++in)
  {
    // if already set, ignore
    if (in->second.stream_id > 0) continue;

    // expects "file_id:stream_spec"
    const std::string &s = in->first;

    size_t sep_pos;
    int file_id, stream_id;

    try
    {
      file_id = (int)std::stoul(s, &sep_pos);
      if (file_id >= fmts.size())
        throw Exception("Filter input link label requests invalid input file.");
      const InputFormat &fmt = *fmts[file_id];
      stream_id = fmt.getStreamId(s.substr(sep_pos + 1));
      if (fmt.isStreamActive(stream_id))
        throw Exception("Filtergraph cannot use the specified input "
                        "stream as it has already been taken.");
      if (in->second.type != fmt.getStreamType(stream_id))
        throw Exception(
            "Filter input link label does not specify the correct media type.");
      in->second.file_id = file_id;
      in->second.stream_id = stream_id;
    }
    catch (const Exception &e)
    {
      throw e;
    }
    catch (...)
    {
      invalid_names.push_back(s);
    }
  }

  // for all unmatched inputs, find the first unused stream of corresponding
  // type
  for (auto in_name = invalid_names.begin(); in_name != invalid_names.end();
       ++in_name)
  {
    SourceInfo &in_info = inputs[*in_name];
    bool notfound = true;
    for (auto p_fmt = fmts.begin(); p_fmt != fmts.end() && notfound; ++p_fmt)
    {
      auto fmt = *p_fmt;
      for (int i = 0; i < fmt->getNumberOfStreams() && notfound; ++i)
      {
        if (fmt->isStreamActive(i)) continue;
        if (in_info.type == fmt->getStreamType(i))
        {
          in_info.file_id = (int)(p_fmt - fmts.begin());
          in_info.stream_id = i;
          notfound = false;
        }
      }
    }
    if (notfound) throw Exception("The filter graph has too many input links.");
  }
}

std::string Graph::getNextUnassignedSourceLink(int *file_id, int *stream_id,
                                               const std::string &last)
{
  auto in = last.size() ? inputs.find(last) : inputs.begin();
  if (in == inputs.end()) return "";

  if (last.size()) ++in;
  for (; in != inputs.end() && in->second.buf; ++in)
    ;

  // return the name & id's if found
  auto in_info = (in != inputs.end()) ? &(in->second) : nullptr;
  if (file_id) *file_id = in_info ? in_info->file_id : -1;
  if (stream_id) *stream_id = in_info ? in_info->stream_id : -1;
  return in_info ? in->first : "";
}

int Graph::processFrame()
{
  // grab one frame from at least one input buffers
  bool noframe = true;
  for (auto src = inputs.begin(); src != inputs.end(); ++src)
  {
    // if buffer is empty, skip
    if (src->second.buf->empty()) continue;

    // process an incoming frame (if any)
    int ret = src->second.filter
                  ->processFrame(); // calls av_buffersrc_add_frame_flags() if
                                    // frame avail
    if (ret < 0 && ret != AVERROR_EOF && ret != AVERROR(EAGAIN))
      throw Exception("[ffmpeg::filter::Graph::runOnce] Failed to "
                      "process a filter graph input AVFrame.");
    else if (noframe)
      noframe = false;
  }

  // if no frame arrived, nothing else to do
  if (noframe) return -1;

  // process all the frames in the output buffers
  int nb_frames = 0;
  do
  {
    noframe = true;
    for (auto out = outputs.begin(); out != outputs.end();
         ++out) // for each buffer
    {
      // if output buffer is full, cannot retrieve more frames
      if (out->second.buf->full()) break;

      SinkBase *sink = out->second.filter;
      int ret = sink->processFrame(); // calls av_buffersink_get_frame() if
                                      // frame avail
      if (ret == 0 || ret == AVERROR_EOF)
      {
        if (ret == 0)
          ++nb_frames; // if success, set the flag to indicate the arrival
        if (noframe) noframe = false;
      }
      else if (ret != AVERROR(EAGAIN))
      {
        throw Exception("[ffmpeg::filter::Graph::runOnce] Failed to "
                        "process a filter graph output AVFrame.");
      }
    }
  } while (!noframe);

  return nb_frames;
}

void Graph::setPixelFormat(const AVPixelFormat pix_fmt, const std::string &spec)
{
  if (spec.size()) // for a specific sink, throws if invalid spec/pix_fmt
  {
    try
    {
      dynamic_cast<VideoSink *>(outputs.at(spec).filter)
          ->setPixelFormat(pix_fmt);
    }
    catch (const std::out_of_range &)
    {
      throw InvalidStreamSpecifier(spec);
    }
    catch (const std::bad_cast &)
    {
      throw UnexpectedMediaType(AVMEDIA_TYPE_VIDEO,
                                outputs.at(spec).filter->getMediaType());
    }
  }
  else // all video sinks, should throw only if invalid pix_fmt
  {
    for (auto out = outputs.begin(); out != outputs.end(); ++out)
    {
      auto filter = out->second.filter;
      if (filter->getMediaType() == AVMEDIA_TYPE_VIDEO)
        dynamic_cast<VideoSink *>(filter)->setPixelFormat(pix_fmt);
    }
  }
}

std::string Graph::findSourceLink(int file_id, int stream_id)
{
  auto in = inputs.begin();
  for (; in != inputs.end() &&
         (in->second.file_id != file_id || in->second.file_id != file_id);
       ++in)
    ;
  return (in != inputs.end()) ? in->first : "";
}
