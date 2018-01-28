#pragma once

#include "ThreadBase.h"
#include "ffmpegBase.h"
#include "ffmpegAVFrameBufferInterfaces.h"

// #include "mexClassHandler.h"
// #include "ffmpegPtrs.h"
// #include "ffmpegAvRedefine.h"
// #include "ffmpegAVFramePtrBuffer.h"
// #include "ffmpegFrameBuffers.h"

extern "C" {
// #include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavutil/pixdesc.h>
}

namespace ffmpeg
{

class FilterGraph : public Base, public ThreadBase
{
public:
  FilterGraph( const std::string &filtdesc = "", const AVPixelFormat pix_fmt = AV_PIX_FMT_NONE);
  ~FilterGraph();

  const std::string &getFilterGraph() const { return filter_descr; } // stops
  void setFilterGraph(const std::string &filter_desc, const AVPixelFormat pix_fmt = AV_PIX_FMT_NONE); // stops 

  const AVPixelFormat getPixelFormat() const { return pix_fmt; };

protected:
  // thread function: responsible to read packet and send it to ffmpeg decoder
  void thread_fcn();


private:
  void create_filters(const std::string &descr="", const AVPixelFormat pix_fmt = AV_PIX_FMT_NONE);
  void destroy_filters();

  AVFilterGraph *filter_graph;
  AVFilterContext *buffersrc_ctx;
  AVFilterContext *buffersink_ctx;

};
}
