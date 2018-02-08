#include "mexImageFilter.h"

extern "C" {
#include <libswscale/swscale.h>
// #include <libavfilter/avfiltergraph.h>
// #include <libavcodec/avcodec.h>
// #include <libavutil/pixfmt.h>
// #include <libavutil/pixdesc.h>
}

#include <fstream>
std::ofstream output("mextest.csv");
void mexFFmpegCallback(void *avcl, int level, const char *fmt, va_list argptr)
{
  if (level <= AV_LOG_ERROR) //AV_LOG_FATAL || level == AV_LOG_ERROR)
  {
    char dest[1024 * 16];
#ifdef _MSC_VER
    vsprintf_s(dest, 1024 * 16, fmt, argptr);
#else
    vsprintf(dest, fmt, argptr);
#endif
    mexPrintf(dest);
    output << dest << std::endl;
  }
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{

  av_log_set_callback(&mexFFmpegCallback);

  mexClassHandler<mexImageFilter>(nlhs, plhs, nrhs, prhs);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

mexImageFilter::mexImageFilter(int nrhs, const mxArray *prhs[]) {}
mexImageFilter::~mexImageFilter() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

void mexImageFilter::set_prop(const mxArray *, const std::string name, const mxArray *value)
{
  if (name == "FilterGraph") // integer between -10 and 10
  {
    init(mexGetString(value));
  }
  else
  {
    throw std::runtime_error(std::string("Unknown property name:") + name);
  }
}

mxArray *mexImageFilter::get_prop(const mxArray *, const std::string name)
{
  mxArray *rval;
  if (name == "FilterGraph") // integer between -10 and 10
  {
    rval = mxCreateString(filtergraph.getFilterGraphDesc().c_str());
  }
  else if (name == "InputNames")
  {
    string_vector inputs = filtergraph.getInputNames();
    rval = mxCreateCellMatrix(inputs.size(), 1);
    for (int i = 0; i < inputs.size(); ++i)
      mxSetCell(rval, i, mxCreateString(inputs[i].c_str()));
  }
  else if (name == "OutputNames")
  {
    string_vector outputs = filtergraph.getOutputNames();
    rval = mxCreateCellMatrix(outputs.size(), 1);
    for (int i = 0; i < outputs.size(); ++i)
      mxSetCell(rval, i, mxCreateString(outputs[i].c_str()));
  }
  else
  {
    throw std::runtime_error(std::string("Unknown property name:") + name);
  }
  return rval;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool mexImageFilter::action_handler(const mxArray *mxObj, const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  // try the base class action (set & get) first, returns true if action has been performed
  if (mexFunctionClass::action_handler(mxObj, command, nlhs, plhs, nrhs, prhs))
    return true;

  if (command == "runSimple")
    runSimple(nlhs, plhs, nrhs, prhs);
  else if (command == "runComplex")
    runComplex(nlhs, plhs, nrhs, prhs);
  else if (command == "reset")
    reset();
  else if (command == "isSimple")
    plhs[0] = mxCreateLogicalScalar(filtergraph.isSimple());
  return true;
}

void mexImageFilter::runSimple(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
}

void mexImageFilter::runComplex(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
}

void mexImageFilter::reset()
{
  filtergraph.destroy();
}

void mexImageFilter::init(const std::string &new_graph)
{
  av_log(NULL,AV_LOG_ERROR,"Starting init with filter graph: %s\n",new_graph);

  // release data in buffers
  for (auto src = sources.begin(); src < sources.end(); ++src) // release previously allocated resources
    src->clear();
  for (auto sink = sinks.begin(); sink < sinks.end(); ++sink) // release previously allocated resources
    sink->clear(true);

  av_log(NULL,AV_LOG_ERROR,"Parsing the graph...\n");

  // create the new graph (automatically destroys previous one)
  filtergraph.parse(new_graph);

  av_log(NULL,AV_LOG_ERROR,"Creating source buffers...\n");

  // create additional source & sink buffers and assign'em to filtergraph's named input & output pads
  string_vector ports = filtergraph.getInputNames();
  sources.reserve(ports.size());
  av_log(NULL,AV_LOG_ERROR,"Allocating %d buffers...\n", ports.size());
  for (size_t i = sources.size(); i < ports.size(); ++i) // create more source buffers if not enough available
    sources.emplace_back();
  av_log(NULL,AV_LOG_ERROR,"Associating buffers to the source filters...\n", ports.size());
  for (size_t i = 0; i < ports.size(); ++i) // link the source buffer to the filtergraph
    filtergraph.assignSource(ports[i], sources[i]);

  av_log(NULL,AV_LOG_ERROR,"Creating sink buffers...\n");

  ports = filtergraph.getOutputNames();
  sinks.reserve(ports.size());
  for (size_t i = sinks.size(); i < sinks.size(); ++i) // new source
    sinks.emplace_back();
  for (size_t i = 0; i < sinks.size(); ++i) // new source
    filtergraph.assignSink(ports[i], sinks[i]);

  av_log(NULL,AV_LOG_ERROR,"Finalizing filtergraph construction...\n");
  // configure
  filtergraph.configure();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool mexImageFilter::static_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  if (command == "getFilters")
  {
    mexImageFilter::getFilters(nlhs, plhs);
  }
  else if (command == "getFormats")
  {
    mexImageFilter::getVideoFormats(nlhs, plhs);
  }
  else
    return false;
  return true;
}

void mexImageFilter::getFilters(int nlhs, mxArray *plhs[])
{
  // make sure all the filters are registered
  avfilter_register_all();

  ::getFilters(nlhs, plhs, [](const AVFilter *filter) -> bool {

    if (strcmp(filter->name, "buffer") || strcmp(filter->name, "buffersink") || strcmp(filter->name, "fifo"))
      return false;

    // filter must be a non-audio filter
    bool dyn[2] = {bool(filter->flags & AVFILTER_FLAG_DYNAMIC_INPUTS), bool(filter->flags & AVFILTER_FLAG_DYNAMIC_OUTPUTS)};
    for (int i = 0; i < 2; ++i)
    {
      if (dyn[i])
        continue;
      const AVFilterPad *pad = i ? filter->outputs : filter->inputs;
      for (int j = 0; pad && avfilter_pad_get_name(pad, j); ++j)
        if (avfilter_pad_get_type(pad, j) == AVMEDIA_TYPE_AUDIO)
          return false;
    }
    return true;
  });
}

void mexImageFilter::getVideoFormats(int nlhs, mxArray *plhs[])
{
  ::getVideoFormats(nlhs, plhs, [](const AVPixelFormat pix_fmt) -> bool {
    // must <= 8-bit/component
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    if (!desc || desc->flags & AV_PIX_FMT_FLAG_BITSTREAM) // invalid format
      return false;

    // depths of all components must be single-byte
    for (int i = 0; i < desc->nb_components; ++i)
      if (desc->comp[i].depth > 8)
        return false;

    // supported by SWS library
    return sws_isSupportedInput(pix_fmt) && sws_isSupportedOutput(pix_fmt);
  });
}
