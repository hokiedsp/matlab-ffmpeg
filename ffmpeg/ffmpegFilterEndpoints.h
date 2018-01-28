#pragma once

#include "ffmpegAVFrameBufferInterfaces.h"

extern "C" {
// #include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavutil/pixdesc.h>
}

namespace ffmpeg
{

inline void delete_filter_inout(AVFilterInOut *filter_inout)
{
  if (filter_inout)
    avfilter_inout_free(&filter_inout);
}
typedef std::unique_ptr<AVFilterInOut, decltype(&delete_filter_inout)> AVFilterInOutPtr;

class FilterEndpoint
{
public:
  FilterEndpoint();
  ~FilterEndpoint();

  AVFilterInOutPtr get
protected:
  AVFilterContext *ctx;
};
}
