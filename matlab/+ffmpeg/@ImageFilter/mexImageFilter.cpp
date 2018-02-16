#include "mexImageFilter.h"
#include "ffmpegAVFrameBufferInterfaces.h"

#include "mexParsers.h"

extern "C" {
#include <libswscale/swscale.h>
// #include <libavfilter/avfiltergraph.h>
// #include <libavcodec/avcodec.h>
// #include <libavutil/pixfmt.h>
// #include <libavutil/pixdesc.h>
}

#include <stdexcept>
#include <memory>

#include <fstream>
std::ofstream output("mextest.csv");
void mexFFmpegCallback(void *avcl, int level, const char *fmt, va_list argptr)
{
  if (level <= AV_LOG_TRACE) //AV_LOG_ERROR) //AV_LOG_FATAL || level == AV_LOG_ERROR)
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

mexImageFilter::mexImageFilter(int nrhs, const mxArray *prhs[]) : ran(false), changedFormat(false), changedSAR(false) {}
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
    runSimple(mxObj, plhs[0], prhs[0]);
  else if (command == "runComplex")
    runComplex(mxObj, plhs[0], prhs[0]);
  else if (command == "reset")
    reset();
  else if (command == "isSimple")
    plhs[0] = mxCreateLogicalScalar(filtergraph.isSimple());
  else if (command == "isValidInputName")
    plhs[0] = isValidInputName(prhs[0]);
  else if (command == "syncInputFormat")
    syncInputFormat(mxObj);
  else if (command == "syncInputSAR")
    syncInputSAR(mxObj);

  return true;
}

// outimg = runSimple(inimg)
void mexImageFilter::runSimple(const mxArray *mxObj, mxArray *&mxOut, const mxArray *mxIn)
{
  // check to make sure filter graph is ready to go: AVFilterGraph is present and SourceInfo and SinkInfo maps are fully populated
  filtergraph.ready();

  // get the input & output buffer
  mexComponentSource &src = *dynamic_cast<mexComponentSource *>(filtergraph.getInputBuffer());
  mexComponentSink &sink = *dynamic_cast<mexComponentSink *>(filtergraph.getOutputBuffer());

  // get the input image (guaranteed to be nonempty uint8 array)
  int width, height, depth;
  const uint8_t *in = mexImageFilter::getMxImageData(mxIn, width, height, depth);

  // check to see if width or height changed
  bool changedDims = (ran && (width != src.getWidth() || height != src.getHeight()));

  bool config = !ran;
  bool reconfig = ran || changedFormat || changedSAR || changedDims;
  if (ran) // clear ran flag to sync with Matlab OBJ
    ran = false;

  // sync format & sar if changed in MATLAB
  if (changedFormat)
    syncInputFormat(mxObj);
  if (changedSAR)
    syncInputSAR(mxObj);

  // check the depth against the format
  const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(src.getFormat());
  if (desc->nb_components != depth)
    throw std::runtime_error("The depth of the image data does not match the image format's.");

  // set the buffer's dimensional parameters
  src.setWidth(width);
  src.setHeight(height);

  av_log(NULL, AV_LOG_INFO, "format:%s:width:%d:height:%d:sar:%d:%d:time_base:%d:%d\n", src.getFormatName(), src.getWidth(),
         src.getHeight(), src.getSAR().num, src.getSAR().den, src.getTimeBaseRef().num, src.getTimeBaseRef().den);

  // configure the filter graph
  if (config)
  {
    filtergraph.configure(); // new graph
    ran = true;
  }
  else if (reconfig)
    filtergraph.flush(); // recreate AVFilterGraph with the same AVFrame buffers

  // send the image data to the buffer
  av_log(NULL, AV_LOG_INFO, "[runOnce] Loading the input data...\n");
  src.load(in, (int)mxGetNumberOfElements(mxIn));

  // make sure everything is ready to go
  av_log(NULL, AV_LOG_INFO, "[runOnce] Final check...\n");
  if (!filtergraph.ready()) // something went wrong
    throw std::runtime_error("Failed to configure the filter graph.");

  // run the filter
    av_log(NULL, AV_LOG_INFO, "[runOnce] RUN!!...\n");
  filtergraph.runOnce();

// test direct transfer
// auto avFrameFree = [](AVFrame *frame) { av_frame_free(&frame); };
// std::unique_ptr<AVFrame, decltype(avFrameFree)> frame(av_frame_alloc(), avFrameFree);
// src.pop(frame.get());

// get the output
av_log(NULL, AV_LOG_INFO, "[runOnce] Retrieve the output data...\n");
uint8_t *data;
// sink.push(frame.get());
if (!sink.release(&data)) // grab entire the data buffer
  throw std::runtime_error("No output data were produced by the filter graph.");

// output format
desc = av_pix_fmt_desc_get(sink.getFormat());
mwSize dims[3] = {(mwSize)sink.getWidth(), (mwSize)sink.getHeight(), (mwSize)desc->nb_components};
mxOut = mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL);
mxSetDimensions(mxOut, dims, 3);
mxSetData(mxOut, data);
}

// Soutimg = runComplex(Sinimg)
void mexImageFilter::runComplex(const mxArray *mxObj, mxArray *&mxOut, const mxArray *mxIn)
{
  // check to make sure filter graph is ready to go: AVFilterGraph is present and SourceInfo and SinkInfo maps are fully populated
  filtergraph.ready();

  // check to see if width or height changed
  bool config = !ran;
  bool changedDims = ran;
  if (ran) // clear ran flag to sync with Matlab OBJ
    ran = false;

  // sync format & sar if changed in MATLAB
  if (changedFormat)
    syncInputFormat(mxObj);
  if (changedSAR)
    syncInputSAR(mxObj);

  // get the input & output buffer
  filtergraph.forEachInputBuffer([&](const std::string &name, ffmpeg::IAVFrameSource *srcbuf) {
    mexComponentSource &src = *dynamic_cast<mexComponentSource *>(srcbuf);

    // grab the input image array (field name )
    mxArray *mxInImg = mxGetField(mxIn, 0, name.c_str());
    if (!mxInImg) // if input image not given, use one from the previous run
      return;

    // get the input image (guaranteed to be nonempty uint8 array)
    ffmpeg::VideoParams params = {AV_PIX_FMT_NONE, 0, 0, {0, 0}};
    int depth;
    const uint8_t *in = mexImageFilter::getMxImageData(mxInImg, params.width, params.height, depth);

    // check the depth against the format
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(src.getFormat());
    if (desc->nb_components != depth)
      throw std::runtime_error("The depth of the image data does not match the image format's.");

    // send the image data to the buffer
    av_log(NULL, AV_LOG_INFO, "[runComplex] Loading the data to input '%s'...\n", name.c_str());
    src.load(params, in, (int)mxGetNumberOfElements(mxIn));
    av_log(NULL, AV_LOG_INFO, "\tformat:%s:width:%d:height:%d:sar:%d:%d:time_base:%d:%d\n", src.getFormatName(), src.getWidth(),
           src.getHeight(), src.getSAR().num, src.getSAR().den, src.getTimeBaseRef().num, src.getTimeBaseRef().den);

    // check to see if width or height changed

    if (!changedDims && (params.width != src.getWidth() || params.height != src.getHeight()))
      changedDims = true;
  });

  // configure the filter graph
  if (config)
  {
    filtergraph.configure(); // new graph
    ran = true;
  }
  else
  {
    bool reconfig = changedFormat || changedSAR || changedDims;
    if (reconfig)
      filtergraph.flush(); // recreate AVFilterGraph with the same AVFrame buffers
  }

  // make sure everything is ready to go
  av_log(NULL, AV_LOG_INFO, "[runOnce] Final check...\n");
  if (!filtergraph.ready()) // something went wrong
    throw std::runtime_error("Failed to configure the filter graph.");

  // run the filter
  av_log(NULL, AV_LOG_INFO, "[runOnce] RUN!!...\n");
  filtergraph.runOnce();

  // create output struct array
  mxOut = mxCreateStructMatrix(1, 1, 0, NULL);
  if (!mxOut)
    throw std::runtime_error("Failed to create output stuct array.");

  // get the output
  av_log(NULL, AV_LOG_INFO, "[runOnce] Retrieve the output data...\n");
  bool fail = false;
  filtergraph.forEachOutputBuffer([&](const std::string &name, ffmpeg::IAVFrameSink *sinkbuf) {
    if (fail) return;

    mexComponentSink &sink = *dynamic_cast<mexComponentSink *>(sinkbuf);
    uint8_t *data;
    if (!sink.release(&data)) // grab entire the data buffer
    {
      fail = true;
      return;
    }

    // output format
    mwSize dims[3] = {(mwSize)sink.getWidth(), (mwSize)sink.getHeight(), (mwSize)av_pix_fmt_desc_get(sink.getFormat())->nb_components};
    mxArray *mxOutImg = mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL);
    mxSetDimensions(mxOutImg, dims, 3);
    mxSetData(mxOutImg, data);

    int fid = mxAddField(mxOut, name.c_str());
    if (fid < 0)
      throw std::runtime_error("failed to add a new output struct field.");
    mxSetFieldByNumber(mxOut, 0, fid, mxOutImg);
  });
  if (fail)
    throw std::runtime_error("Not all output data were produced by the filter graph.");
}

const uint8_t *mexImageFilter::getMxImageData(const mxArray *mxData, int &width, int &height, int &depth)
{
  // width & height are presented backwards to account for difference in column/row-major data storage
  const mwSize *dims = mxGetDimensions(mxData);
  width = (int)dims[0];
  height = (int)dims[1];
  depth = mxGetNumberOfDimensions(mxData) < 3 ? 1 : (int)dims[2];
  return (uint8_t *)mxGetData(mxData);
}

void mexImageFilter::syncInputFormat(const mxArray *mxObj)
{
  mxArray *mxFmt = mxGetProperty(mxObj, 0, "InputFormat");
  if (mxIsStruct(mxFmt))
  {
    filtergraph.forEachInputBuffer([&](const std::string &name, ffmpeg::IAVFrameSource *buf) {
      std::string fmt_str = mexGetString(mxGetField(mxFmt, 0, name.c_str()));
      AVPixelFormat fmt = av_get_pix_fmt(fmt_str.c_str());
      mexComponentSource *src = dynamic_cast<mexComponentSource *>(buf);
      if (!ran)
        src->setFormat(fmt);
      else if (!changedFormat && fmt != src->getFormat())
        changedFormat = true;
    });
  }
  else
  {
    std::string fmt_str = mexGetString(mxFmt);
    AVPixelFormat fmt = av_get_pix_fmt(fmt_str.c_str());
    filtergraph.forEachInputBuffer([&](const std::string &name, ffmpeg::IAVFrameSource *buf) {
      mexComponentSource *src = dynamic_cast<mexComponentSource *>(buf);
      if (!ran)
        src->setFormat(fmt);
      else if (!changedFormat && fmt != src->getFormat())
        changedFormat = true;
    });
  }
  if (!ran && changedFormat) // sync'd
    changedFormat = false;

  av_log(NULL, AV_LOG_INFO, "InputFormat synchronized.\n");
}

void mexImageFilter::syncInputSAR(const mxArray *mxObj)
{
  mxArray *mxSAR = mxGetProperty(mxObj, 0, "InputSAR");

  if (mxIsStruct(mxSAR))
  {
    filtergraph.forEachInputBuffer([&](const std::string &name, ffmpeg::IAVFrameSource *buf) {
      AVRational sar = mexImageFilter::getSAR(mxGetField(mxSAR, 0, name.c_str()));
      mexComponentSource *src = dynamic_cast<mexComponentSource *>(buf);
      if (!ran)
        src->setSAR(sar);
      else if (!changedSAR && av_cmp_q(sar, src->getSAR()))
        changedSAR = true;
    });
  }
  else
  {
    AVRational sar = mexImageFilter::getSAR(mxSAR);
    filtergraph.forEachInputBuffer([&](const std::string &name, ffmpeg::IAVFrameSource *buf) {
      mexComponentSource *src = dynamic_cast<mexComponentSource *>(buf);
      if (!ran)
        src->setSAR(sar);
      else if (!changedSAR && av_cmp_q(sar, src->getSAR()))
        changedSAR = true;
    });
  }
  if (!ran && changedSAR)
    changedSAR = false; // sync'ed

  av_log(NULL, AV_LOG_INFO, "InputSAR synchronized.\n");
}

void mexImageFilter::reset()
{
  filtergraph.destroy();
}

void mexImageFilter::init(const std::string &new_graph)
{
  // release data in buffers
  for (auto src = sources.begin(); src < sources.end(); ++src) // release previously allocated resources
    src->clear();
  for (auto sink = sinks.begin(); sink < sinks.end(); ++sink) // release previously allocated resources
    sink->clear(true);

  // create the new graph (automatically destroys previous one)
  filtergraph.parse(new_graph);

  // create additional source & sink buffers and assign'em to filtergraph's named input & output pads
  string_vector ports = filtergraph.getInputNames();
  sources.reserve(ports.size());
  for (size_t i = sources.size(); i < ports.size(); ++i) // create more source buffers if not enough available
    sources.emplace_back();
  for (size_t i = 0; i < ports.size(); ++i) // link the source buffer to the filtergraph
    filtergraph.assignSource(sources[i], ports[i]);

  ports = filtergraph.getOutputNames();
  sinks.reserve(ports.size());
  for (size_t i = sinks.size(); i < ports.size(); ++i) // new source
    sinks.emplace_back();
  for (size_t i = 0; i < ports.size(); ++i) // new source
    filtergraph.assignSink(sinks[i], ports[i]);

  ran = false;
}

mxArray *mexImageFilter::isValidInputName(const mxArray *prhs) // tf = isInputName(obj,name)
{
  return mxCreateLogicalScalar(filtergraph.isSource(mexGetString(prhs)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/// StATIC ROUTINES

bool mexImageFilter::static_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  if (command == "getFilters")
    plhs[0] = mexImageFilter::getFilters();
  else if (command == "getFormats")
    plhs[0] = mexImageFilter::getFormats();
  else if (command == "isSupportedFormat")
    plhs[0] = mexImageFilter::isSupportedFormat(prhs[0]);
  else if (command == "validateSARString")
    mexImageFilter::validateSARString(prhs[0]);
  else
    return false;
  return true;
}

mxArray *mexImageFilter::getFilters()
{
  // make sure all the filters are registered
  avfilter_register_all();

  return ::getFilters([](const AVFilter *filter) -> bool {

    if (!(strcmp(filter->name, "buffer") && strcmp(filter->name, "buffersink") && strcmp(filter->name, "fifo")))
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

mxArray *mexImageFilter::getFormats()
{
  return getVideoFormats([](const AVPixelFormat pix_fmt) -> bool {
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

// tf = isSupportedFormat(format_name);
mxArray *mexImageFilter::isSupportedFormat(const mxArray *prhs)
{
  return ::isSupportedVideoFormat(prhs, [](const AVPixelFormat pix_fmt) -> bool {
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

// validateSARString(SAR_expression);
void mexImageFilter::validateSARString(const mxArray *prhs)
{
  AVRational sar = mexParseRatio(prhs);
  if (sar.num <= 0 || sar.den <= 0)
    mexErrMsgTxt("SAR expression must result in a positive rational number.");
}

AVRational mexImageFilter::getSAR(const mxArray *mxSAR)
{
  if (mxIsScalar(mxSAR))
  {
    return av_d2q(mxGetScalar(mxSAR), INT_MAX);
  }
  else if (mxIsChar(mxSAR))
  {
    return mexParseRatio(mxSAR);
  }
  else // 2-elem
  {
    double *data = mxGetPr(mxSAR);
    return av_make_q((int)data[0], (int)data[1]);
  }
}