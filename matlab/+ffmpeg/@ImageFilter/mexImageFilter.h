#pragma once

#include "mexParsers.h"
#include "mexClassHandler.h"
#include "mexAllocator.h"
#include "ffmpegAVFrameImageComponentSource.h"
#include "ffmpegAVFrameVideoComponentSink.h"
#include "filter/ffmpegFilterGraph.h"
#include "mexGetFilters.h"
#include "mexGetVideoFormats.h"

#include <vector>

typedef std::vector<uint8_t> uint8_vector;

class mexImageFilter : public mexFunctionClass
{
public:
  mexImageFilter(int nrhs, const mxArray *prhs[]);
  virtual ~mexImageFilter();

  static std::string get_classname() { return "ffmpeg.ImageFilter"; };
  static std::string get_componentid() { return "ImageFilter.mexfcn"; }

  bool action_handler(const mxArray *mxObj, const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);
  static bool static_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);

protected:
  void set_prop(const mxArray *, const std::string name, const mxArray *value);
  mxArray *get_prop(const mxArray *, const std::string name);

  void reset();                                                                // reset(obj);

  void runSimple(const mxArray *mxObj, mxArray *&out, const mxArray *in);  //    out = runSimple(obj, in);
  void runComplex(const mxArray *mxObj, mxArray *&out, const mxArray *in); //    varargout = readFrame(obj, varargin);
  const uint8_t *getMxImageData(const mxArray *mxData, int &width, int &height, int &depth);

  mxArray *isValidInputName(const mxArray *prhs); // tf = isInputName(obj,name)

  void syncInputFormat(const mxArray *mxObj);
  void syncInputSAR(const mxArray *mxObj);

/////////////////

  static mxArray *getFilters(); // formats = getFilters();
  static mxArray *getFormats(); // formats = getVideoFormats();
  static mxArray *isSupportedFormat(const mxArray *prhs); // tf = isSupportedFormat(format_name);
  static void validateSARString(const mxArray *prhs); // tf = isValidSAR(SARexpr);
  static AVRational getSAR(const mxArray *mxObj);

private:
  void init(const std::string &new_graph);

  bool ran;     // true if filter graph has run with the current configuration
  bool changedFormat; // true if there is a pending change on InputFormat
  bool changedSAR;    // true if there is a pending change on InputSAR

  ffmpeg::filter::Graph filtergraph;
  typedef ffmpeg::AVFrameImageComponentSource mexComponentSource;
  typedef std::vector<mexComponentSource> mexComponentSources;
  mexComponentSources sources;

  typedef ffmpeg::AVFrameVideoComponentSink<mexAllocator<uint8_t>> mexComponentSink;
  typedef std::vector<mexComponentSink, mexAllocator<mexComponentSink>> mexComponentSinks;
  mexComponentSinks sinks;
};
