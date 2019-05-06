#pragma once

#include <mexParsers.h>
#include "ffmpegAVFrameImageComponentSource.h"
#include "ffmpegAVFrameVideoComponentSink.h"
#include "filter/ffmpegFilterGraph.h"
#include "mexGetFilters.h"
#include "mexGetVideoFormats.h"

#include <mexObjectHandler.h>
#include <mexAllocator.h>

#include <vector>

typedef std::vector<uint8_t> uint8_vector;

class mexImageFilter
{
public:
  mexImageFilter(const mxArray *mxObj, int nrhs, const mxArray *prhs[]);
  virtual ~mexImageFilter();

  static std::string get_classname() { return "ffmpeg.ImageFilter"; };

  bool action_handler(const mxArray *mxObj, const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);
  static bool static_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);

private:
  void reset();                                                                // reset(obj);

  void runSimple(const mxArray *mxObj, int nout, mxArray **out, const mxArray *in);  //    out = runSimple(obj, in);
  void runComplex(const mxArray *mxObj, int nout, mxArray **out, const mxArray *in); //    varargout = readFrame(obj, varargin);
  
  mxArray *isValidInputName(const mxArray *prhs); // tf = isInputName(obj,name)

  void syncInputFormat(const mxArray *mxObj);
  void syncInputSAR(const mxArray *mxObj);

  void configPrefilters(const mxArray *mxObj); // AutoTranspose, OutputFormat
  /////////////////

  static mxArray *getFilters();                           // formats = getFilters();
  static mxArray *getFormats();                           // formats = getVideoFormats();
  static mxArray *isSupportedFormat(const mxArray *prhs); // tf = isSupportedFormat(format_name);
  static void validateSARString(const mxArray *prhs); // tf = isValidSAR(SARexpr);
  static AVRational getSAR(const mxArray *mxObj);

  void init(const mxArray *mxObj, const std::string &new_graph);
  const uint8_t *getMxImageData(const mxArray *mxData, int &width, int &height, int &depth);

  bool ran;           // true if filter graph has run with the current configuration
  bool changedInputFormat; // true if there is a pending change on InputFormat
  bool changedInputSAR;    // true if there is a pending change on InputSAR
  bool changedOutputFormat; // true if there is a pending change on OutputFormat 
  bool changedAutoTranspose; // true if there is a pending change on AutoTranspose

  ffmpeg::filter::Graph filtergraph;
  typedef ffmpeg::AVFrameImageComponentSource mexComponentSource;
  typedef std::vector<mexComponentSource> mexComponentSources;
  mexComponentSources sources;

  typedef ffmpeg::AVFrameVideoComponentSink<mexAllocator<uint8_t>> mexComponentSink;
  typedef std::vector<mexComponentSink, mexAllocator<mexComponentSink>> mexComponentSinks;
  mexComponentSinks sinks;
};
