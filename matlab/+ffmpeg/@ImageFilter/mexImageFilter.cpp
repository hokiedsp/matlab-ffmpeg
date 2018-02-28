#include "mexImageFilter.h"
#include "ffmpegAVFrameBufferInterfaces.h"

#include "mexParsers.h"

#include "ffmpegLogUtils.h"

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

  // av_log_set_callback(&mexFFmpegCallback);

  mexObjectHandler<mexImageFilter>(nlhs, plhs, nrhs, prhs);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

mexImageFilter::mexImageFilter(int nrhs, const mxArray *prhs[])
    : ran(false), changedInputFormat(true), changedInputSAR(true),
      changedOutputFormat(true), changedAutoTranspose(true) {}
mexImageFilter::~mexImageFilter() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool mexImageFilter::action_handler(const mxArray *mxObj, const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  if (command == "setFilterGraph") // integer between -10 and 10
  {
    init(mxObj, mexGetString(prhs[0]));
  }
  else if (command == "getFilterGraph") // integer between -10 and 10
  {
    plhs[0] = mxCreateString(filtergraph.getFilterGraphDesc().c_str());
  }
  else if (command == "getInputNames")
  {
    string_vector inputs = filtergraph.getInputNames();
    plhs[0] = mxCreateCellMatrix(1, inputs.size());
    for (int i = 0; i < inputs.size(); ++i)
      mxSetCell(plhs[0], i, mxCreateString(inputs[i].c_str()));
  }
  else if (command == "getOutputNames")
  {
    string_vector outputs = filtergraph.getOutputNames();
    plhs[0] = mxCreateCellMatrix(1, outputs.size());
    for (int i = 0; i < outputs.size(); ++i)
      mxSetCell(plhs[0], i, mxCreateString(outputs[i].c_str()));
  }
  else if (command == "runSimple")
    runSimple(mxObj, nlhs, plhs, prhs[0]);
  else if (command == "runComplex")
    runComplex(mxObj, nlhs, plhs, prhs[0]);
  else if (command == "reset")
    reset();
  else if (command == "isSimple")
    plhs[0] = mxCreateLogicalScalar(filtergraph.isSimple());
  else if (command == "isValidInputName")
    plhs[0] = isValidInputName(prhs[0]);
  else if (command == "notifyInputFormatChange")
    changedInputFormat = true;
  else if (command == "notifyInputSARChange")
    changedInputSAR = true;
  else if (command == "notifyOutputFormatChange")
    changedOutputFormat = true;
  else if (command == "notifyAutoTransposeChange")
    changedAutoTranspose = true;

  return true;
}

// outimg = runSimple(inimg)
void mexImageFilter::runSimple(const mxArray *mxObj, int nout, mxArray **mxOut, const mxArray *mxIn)
{
  // check to make sure filter graph is ready to go: AVFilterGraph is present and SourceInfo and SinkInfo maps are fully populated
  if (!filtergraph.ready())
    throw std::runtime_error("The filtergraph is not ready for filtering operation.");

  // get the input & output buffer
  mexComponentSource &src = *dynamic_cast<mexComponentSource *>(filtergraph.getInputBuffer());
  mexComponentSink &sink = *dynamic_cast<mexComponentSink *>(filtergraph.getOutputBuffer());

  // get the input image (guaranteed to be nonempty uint8 array)
  int width, height, depth;
  const uint8_t *in = mexImageFilter::getMxImageData(mxIn, width, height, depth);

  // check to see if width or height changed
  bool changedDims = (ran && (width != src.getWidth() || height != src.getHeight()));
  av_log(NULL, AV_LOG_INFO, "ran:%d|changedInputFormat:%d|changedInputSAR:%d|changedDims:%d\n",
         ran, changedInputFormat, changedInputSAR, changedDims);

  bool config = !ran;
  bool reconfig = ran && (changedInputFormat || changedInputSAR || changedDims);
  bool reconfig_prefilter = changedAutoTranspose || changedOutputFormat;
  if (reconfig && ran) // clear ran flag to sync with Matlab OBJ
    ran = false;

  // sync format & sar if changed in MATLAB
  if (changedInputFormat)
    syncInputFormat(mxObj);
  if (changedInputSAR)
    syncInputSAR(mxObj);
  if (reconfig_prefilter)
  {
    configPrefilters(mxObj); // sets prefilter description
    changedAutoTranspose = changedOutputFormat = false;
  }
  // check the depth against the format
  const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(src.getFormat());

  ffmpeg::logPixelFormat(desc, "runSimple");

  if (desc->nb_components != depth)
    throw std::runtime_error("The depth of the image data does not match the image format's.");

  // set the buffer's dimensional parameters
  src.setWidth(width);
  src.setHeight(height);

  ffmpeg::logVideoParams(src.getVideoParams(), "runSimple::src");

  // configure the filter graph
  av_log(NULL, AV_LOG_INFO, "ran:%d|changedInputFormat:%d|changedInputSAR:%d|changedDims:%d\n",
         ran, changedInputFormat, changedInputSAR, changedDims);
  if (config)
  {
    av_log(NULL, AV_LOG_INFO, "[runOnce] Configuring the filter graph\n");
    filtergraph.configure(); // new graph
    ran = true;
  }
  else
  {
    bool reconfig = changedInputFormat || changedInputSAR || changedDims;
    if (reconfig || reconfig_prefilter)
    {
      av_log(NULL, AV_LOG_INFO, "[runOnce] Re-configuring the filter graph\n");
      filtergraph.flush(); // recreate AVFilterGraph with the same AVFrame buffers
      ran = true;
    }
  }

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
  mxOut[0] = mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL);
  mxSetDimensions(mxOut[0], dims, 3);
  mxSetData(mxOut[0], data);

  if (nout > 1) // also output output format
    mxOut[1] = mxCreateString(sink.getFormatName().c_str());
}

// Soutimg = runComplex(Sinimg)
void mexImageFilter::runComplex(const mxArray *mxObj, int nout, mxArray **mxOut, const mxArray *mxIn)
{
  // check to make sure filter graph is ready to go: AVFilterGraph is present and SourceInfo and SinkInfo maps are fully populated
  if (!filtergraph.ready())
    throw std::runtime_error("The filtergraph is not ready for filtering operation.");

  av_log(NULL, AV_LOG_INFO, "[runComplex] Configuring/Updating the filter graph...\n");

  // check to see if width or height changed
  bool config = !ran;
  bool changedDims = ran;
  if (ran) // clear ran flag to sync with Matlab OBJ
    ran = false;

  // sync format & sar if changed in MATLAB
  if (changedInputFormat)
    syncInputFormat(mxObj);
  av_log(NULL, AV_LOG_INFO, "[runComplex] Input format synced...\n");
  if (changedInputSAR)
    syncInputSAR(mxObj);
  
  bool reconfig_prefilter = changedAutoTranspose || changedOutputFormat;
  if (reconfig_prefilter)
  {
    configPrefilters(mxObj);
    changedAutoTranspose = changedOutputFormat = false;
  }

  av_log(NULL, AV_LOG_INFO, "[runComplex] Input SAR synced...\n");

  // get the input & output buffer
  av_log(NULL, AV_LOG_INFO, "[runComplex] Loading inputs...\n");
  filtergraph.forEachInputBuffer([&](const std::string &name, ffmpeg::IAVFrameSource *srcbuf) {
    mexComponentSource &src = *dynamic_cast<mexComponentSource *>(srcbuf);
    logVideoParams(src.getVideoParams(), name);
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
    src.load(params, in, (int)mxGetNumberOfElements(mxInImg));
    av_log(NULL, AV_LOG_INFO, "\tformat:%s:width:%d:height:%d:sar:%d:%d:time_base:%d:%d\n", src.getFormatName(), src.getWidth(),
           src.getHeight(), src.getSAR().num, src.getSAR().den, src.getTimeBaseRef().num, src.getTimeBaseRef().den);

    // check to see if width or height changed
    if (!changedDims && (params.width != src.getWidth() || params.height != src.getHeight()))
      changedDims = true;
  });

  // configure the filter graph
  if (config)
  {
    av_log(NULL, AV_LOG_INFO, "[runComplex] Configuring the filter graph\n");
    filtergraph.configure(); // new graph
    ran = true;
  }
  else
  {
    bool reconfig = ran && (changedInputFormat || changedInputSAR || changedDims);
    if (reconfig || reconfig_prefilter)
    {
      av_log(NULL, AV_LOG_INFO, "[runComplex] Re-configuring the filter graph\n");
      filtergraph.flush(); // recreate AVFilterGraph with the same AVFrame buffers
      ran = true;
    }
  }

  // make sure everything is ready to go
  av_log(NULL, AV_LOG_INFO, "[runOnce] Final check...\n");
  if (!filtergraph.ready()) // something went wrong
    throw std::runtime_error("Failed to configure the filter graph.");

  // run the filter
  av_log(NULL, AV_LOG_INFO, "[runOnce] RUN!!...\n");
  filtergraph.runOnce();

  // create output struct array
  av_log(NULL, AV_LOG_INFO, "[runComplex] Creating output struct\n");
  mxOut[0] = mxCreateStructMatrix(1, 1, 0, NULL);
  if (!mxOut[0])
    throw std::runtime_error("Failed to create output stuct array.");
  if (nout > 1)
  {
    mxOut[1] = mxCreateStructMatrix(1, 1, 0, NULL);
    if (!mxOut[1])
      throw std::runtime_error("Failed to create output format stuct array.");
  }

  // get the output
  av_log(NULL, AV_LOG_INFO, "[runComplex] Retrieve the output data...\n");
  filtergraph.forEachOutputBuffer([&](const std::string &name, ffmpeg::IAVFrameSink *sinkbuf) {

    av_log(NULL, AV_LOG_INFO, "[runComplex] Obtaining the output %s...\n", name.c_str());

    mexComponentSink &sink = *dynamic_cast<mexComponentSink *>(sinkbuf);
    uint8_t *data;
    if (!sink.release(&data)) // grab entire the data buffer
      throw std::runtime_error("Not all the output .");

    // output format
    mwSize dims[3] = {(mwSize)sink.getWidth(), (mwSize)sink.getHeight(), (mwSize)av_pix_fmt_desc_get(sink.getFormat())->nb_components};
    mxArray *mxOutImg = mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL);
    mxSetDimensions(mxOutImg, dims, 3);
    mxSetData(mxOutImg, data);

    int fid = mxAddField(mxOut[0], name.c_str());
    if (fid < 0)
      throw std::runtime_error("failed to add a new output struct field.");
    mxSetFieldByNumber(mxOut[0], 0, fid, mxOutImg);

    if (nout > 1)
    {
      int fid = mxAddField(mxOut[1], name.c_str());
      if (fid < 0)
        throw std::runtime_error("failed to add a new output struct field.");
      mxSetFieldByNumber(mxOut[1], 0, fid, mxCreateString(sink.getFormatName().c_str()));
    }
  });
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
      else if (!changedInputFormat && fmt != src->getFormat())
        changedInputFormat = true;
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
      else if (!changedInputFormat && fmt != src->getFormat())
        changedInputFormat = true;
    });
  }
  if (!ran && changedInputFormat) // sync'd
    changedInputFormat = false;

  av_log(NULL, AV_LOG_INFO, "InputFormat synchronized.\n");
}

void mexImageFilter::configPrefilters(const mxArray *mxObj)
{
  mxArray *mx = mxGetProperty(mxObj, 0, "AutoTranspose");
  
  std::string desc;
  desc.reserve(16); // reserve enough bytes for transpose filter

  bool transpose = *mxGetLogicals(mx);
  if (transpose) desc = "transpose=dir=0";

  av_log(NULL, AV_LOG_INFO, "desc=%s [%d]\n", desc.c_str(), *mxGetLogicals(mx));

  // only AutoTranspose property affects the input prefilter
  filtergraph.forEachInputFilter([&](const std::string &name, ffmpeg::filter::SourceBase *filter) {
    filter->setPrefilter(desc.c_str());
  });

  mx = mxGetProperty(mxObj, 0, "OutputFormat");

  // for OutputFormat filter description except for the format name
  std::string full_desc;
  full_desc.reserve(64); // reserve enough bytes to account for all format types

  // if already transposed, add separator
  if (transpose)
    full_desc = desc + ',';
  full_desc += "format=pix_fmts=";
  
  if (mxIsStruct(mx))
  {
    filtergraph.forEachOutputFilter([&](const std::string &name, ffmpeg::filter::SinkBase *filter) {
      std::string fmt_str = mexGetString(mx);
      if (fmt_str != "auto")
        filter->setPrefilter((full_desc + fmt_str).c_str());
      else
        filter->setPrefilter(desc.c_str());
    });
  }
  else
  {
    const char *filt_str;
    std::string fmt_str = mexGetString(mx);
    if (fmt_str != "auto")
    {
      full_desc += fmt_str;
      filt_str = full_desc.c_str();
    }
    else
      filt_str = desc.c_str();

    filtergraph.forEachOutputFilter([&](const std::string &name, ffmpeg::filter::SinkBase *filter) {
      filter->setPrefilter(filt_str);
    });
  }
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
      else if (!changedInputSAR && av_cmp_q(sar, src->getSAR()))
        changedInputSAR = true;
    });
  }
  else
  {
    AVRational sar = mexImageFilter::getSAR(mxSAR);
    filtergraph.forEachInputBuffer([&](const std::string &name, ffmpeg::IAVFrameSource *buf) {
      mexComponentSource *src = dynamic_cast<mexComponentSource *>(buf);
      if (!ran)
        src->setSAR(sar);
      else if (!changedInputSAR && av_cmp_q(sar, src->getSAR()))
        changedInputSAR = true;
    });
  }
  if (!ran && changedInputSAR)
    changedInputSAR = false; // sync'ed

  av_log(NULL, AV_LOG_INFO, "InputSAR synchronized.\n");
}

void mexImageFilter::reset()
{
  filtergraph.clear();
}

void mexImageFilter::init(const mxArray *mxObj, const std::string &new_graph)
{
  av_log(NULL, AV_LOG_INFO, "initializing filtergraph...\n");
  // create the new graph (automatically destroys previous one)
  filtergraph.parse(new_graph);

  av_log(NULL, AV_LOG_INFO, "new filtergraph successfully parsed...\n");

  // create source & sink buffers and assign'em to filtergraph's named input & output pads
  sources.clear();
  string_vector ports = filtergraph.getInputNames();
  sources.reserve(ports.size());
  for (size_t i = 0; i < ports.size(); ++i) // create more source buffers if not enough available
  {
    sources.emplace_back();
    filtergraph.assignSource(sources[i], ports[i]);
  }

  // create sink buffers and assign'em to filtergraph's named input & output pads
  sinks.clear();
  ports = filtergraph.getOutputNames();
  sinks.reserve(ports.size());
  for (size_t i = 0; i < ports.size(); ++i) // new source
  {
    sinks.emplace_back();
    filtergraph.assignSink(sinks[i], ports[i]);
  }

  // Force uniform obj.InputFormat
  mxArray *mxProp = mxGetProperty(mxObj, 0, "InputFormat");
  if (mxProp && mxIsStruct(mxProp))
  {
    // use the value of the first scalar for all inputs
    mxArray *mxFmt0 = mxDuplicateArray(mxGetFieldByNumber(mxProp, 0, 0));
    mxDestroyArray(mxProp);
    mxSetProperty((mxArray *)mxObj, 0, "InputFormat", mxFmt0);
  }

  // Force uniform obj.InputSAR
  mxProp = mxGetProperty(mxObj, 0, "InputSAR");
  if (mxProp && mxIsStruct(mxProp))
  {
    // use the value of the first scalar for all inputs
    mxArray *mxFmt0 = mxDuplicateArray(mxGetFieldByNumber(mxProp, 0, 0));
    mxDestroyArray(mxProp);
    mxSetProperty((mxArray *)mxObj, 0, "InputSAR", mxFmt0);
  }

  // clear the flags
  ran = false;
  changedInputFormat = true;
  changedInputSAR = true;
  changedAutoTranspose = true;
  changedOutputFormat = true;
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

    // supported by the IO buffers (8-bit, no subsampled components)
    if (!ffmpeg::imageCheckComponentSize(pix_fmt))
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
    if (!ffmpeg::imageCheckComponentSize(pix_fmt))
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