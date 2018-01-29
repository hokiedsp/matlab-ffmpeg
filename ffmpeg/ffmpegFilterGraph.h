#pragma once

#include "ThreadBase.h"
#include "ffmpegBase.h"
#include "ffmpegStreamInput.h"
#include "ffmpegStreamOutput.h"

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

namespace ffmpeg
{

typedef std::vector<InputFilter *> InputFilterPtrs;
typedef std::vector<OutputFilter *> OutputFilterPtrs;

class FilterGraph : public Base, public ThreadBase
{
public:
  FilterGraph(const std::string &filtdesc = "", const AVPixelFormat pix_fmt = AV_PIX_FMT_NONE);
  ~FilterGraph();

/**
 * \brief Destroys the current AVFilterGraph
 */
void clear();

int init_simple_filtergraph(InputStream &ist, OutputStream &ost);
int init_complex_filtergraph(const std::string &new_desc);

static std::string describe_filter_link(AVFilterInOut *inout, int in);
static int insert_filter(AVFilterContext *&last_filter, int &pad_idx,
                         const std::string &filter_name, const std::string &args);
OutputFilter* configure_output_filter(AVFilterInOut *out);
void check_filter_outputs();
InputFilter *configure_input_filter(AVFilterInOut *in);
void cleanup();
void configure();

protected:
// thread function: responsible to read packet and send it to ffmpeg decoder
void thread_fcn();

private:
  AVFilterGraph *graph;
  std::string graph_desc;

  InputFilterPtrs inputs;
  OutputFilterPtrs outputs;

  // AVFilterContext *buffersrc_ctx;
  // AVFilterContext *buffersink_ctx;

  int reconfiguration;
};
}
