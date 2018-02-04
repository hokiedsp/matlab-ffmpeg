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

  template <typename B>
  void assignSource(B &buf);
  template <typename B>
  void assignSource(const std::string &name, B &buf);

  template <typename B>
  void assignSink(B &buf);
  template <typename B>
  void assignSink(const std::string &name, B &buf);

  void configure();

  AVFilterGraph *getAVFilterGraph() const;

  int insert_filter(AVFilterContext *&last_filter, int &pad_idx,
                    const std::string &filter_name, const std::string &args);

protected:
  // thread function: responsible to read packet and send it to ffmpeg decoder
  void thread_fcn();
  
  template <typename F, typename M>
  static F *find_filter(M &map, AVMediaType type);
  template <typename EP, typename VEP, typename AEP, typename... Args>
  void assign_endpoint(EP *&ep, AVMediaType type, Args... args);

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
};
}
}