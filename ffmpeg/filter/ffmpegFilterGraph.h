#pragma once

#include "../ThreadBase.h"
#include "../ffmpegBase.h"
#include "../ffmpegStreamInput.h"
#include "../ffmpegStreamOutput.h"

#include "ffmpegFilterSinks.h"
#include "ffmpegFilterSources.h"

// #include "mexClassHandler.h"
// #include "ffmpegPtrs.h"
// #include "ffmpegAvRedefine.h"
// #include "ffmpegAVFramePtrBuffer.h"
// #include "ffmpegFrameBuffers.h"

extern "C" {
// #include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
// #include <libavutil/pixdesc.h>
}

#include <vector>
#include <map>
#include <utility>

typedef std::vector<std::string> string_vector;

namespace ffmpeg
{
namespace filter
{
class Graph : public ffmpeg::Base, public ThreadBase
{
public:
  Graph(const std::string &filtdesc = "");
  ~Graph();

  /**
 * \brief Destroys the current AVFilterGraph
 */
  void destroy(const bool complete = false);
  void parse(const std::string &new_desc);

  SourceBase &assignSource(const std::string &name, IAVFrameSource &buf)
  {
    SourceInfo& node = inputs.at(name); // throws exception if doesn't exist
    Graph::assign_endpoint<SourceBase, VideoSource, AudioSource>(node.filter, node.type, buf);
    return *node.filter;
  }

  SinkBase &assignSink(const std::string &name, IAVFrameSink &buf)
  {
    SinkInfo& node = outputs.at(name); // throws exception if doesn't exist
    Graph::assign_endpoint<SinkBase, VideoSink, AudioSink>(node.filter, node.type, buf);
    return *node.filter;
  }

  void configure();

  AVFilterGraph *getAVFilterGraph() const { return graph; }

  int insert_filter(AVFilterContext *&last_filter, int &pad_idx,
                    const std::string &filter_name, const std::string &args);

  std::string getFilterGraphDesc() const { return graph_desc; }
  string_vector getInputNames() const { return Graph::get_names(inputs); }
  string_vector getOutputNames() const { return Graph::get_names(outputs); }

  bool isSimple() const { return inputs.size() == 1 && outputs.size() == 1; }

protected:
  // thread function: responsible to read packet and send it to ffmpeg decoder
  void thread_fcn();

  template <typename EP, typename VEP, typename AEP, typename BUFF>
  void assign_endpoint(EP *&ep, AVMediaType type, BUFF &buf)
  {
    // if already defined, destruct existing
    if (ep)
      delete ep;

    // create new filter
    switch (type)
    {
    case AVMEDIA_TYPE_VIDEO:
        av_log(NULL,AV_LOG_ERROR,"creating video source node\n");
      ep = new VEP(*this, buf);
      break;
    case AVMEDIA_TYPE_AUDIO:
      ep = new AEP(*this, buf);
      break;
    default:
      ep = NULL;
      throw ffmpegException("Only video and audio filters are supported at this time.");
    }
    if (!ep)
      throw ffmpegException("[ffmpeg::filter::Graph::assign_endpoint] Failed to allocate a new filter endpoint.");
  }

private:
  AVFilterGraph *graph;
  std::string graph_desc;

  typedef struct
  {
    AVMediaType type;
    SourceBase *filter;
    AVFilterContext *other;
    int otherpad;
  } SourceInfo;
  typedef struct
  {
    AVMediaType type;
    SinkBase *filter;
    AVFilterContext *other;
    int otherpad;
  } SinkInfo;
  std::map<std::string, SourceInfo> inputs;
  std::map<std::string, SinkInfo> outputs;

  std::mutex inmon_m;
  std::condition_variable inmon_cv;
  int inmon_status; // 0:no monitoring; 1:monitor; <0 quit

  void child_thread_fcn(SourceBase *src);

  template <typename InfoMap>
  static string_vector get_names(const InfoMap &map)
  {
    string_vector names;
    for (auto p = map.begin(); p != map.end(); ++p)
      names.emplace_back(p->first);
    return names;
  }

  void parse_sources(AVFilterInOut *ins);
  void parse_sinks(AVFilterInOut *outs);
};
}
}
