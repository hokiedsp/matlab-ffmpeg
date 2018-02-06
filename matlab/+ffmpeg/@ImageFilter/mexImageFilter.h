#pragma once

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
  virtual ~mexImageFilter();
  static std::string get_componentid() { return "ImageFilter.mexfcn"; }

  bool action_handler(const mxArray *mxObj, const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);
  static bool static_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);

protected:
  void set_prop(const std::string name, const mxArray *value);
  mxArray *get_prop(const std::string name);

  void runSimple(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);  //    out = runSimple(obj, in);
  void runComplex(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]); //    varargout = readFrame(obj, varargin);
  void reset();                                                                // reset(obj);

  static void getFilters(int nlhs, mxArray *plhs[]);      // formats = getFilters();
  static void getVideoFormats(int nlhs, mxArray *plhs[]); // formats = getVideoFormats();

private:
  void init(const std::string &new_graph);

  ffmpeg::filter::Graph filtergraph;
  typedef std::vector<ffmpeg::AVFrameImageComponentSource> mexComponentSources;
  mexComponentSources sources;

  typedef ffmpeg::AVFrameVideoComponentSink<mexAllocator<uint8_t>> mexComponentSink;
  typedef std::vector<mexComponentSink, mexAllocator<mexComponentSink>> mexComponentSinks;
  mexComponentSinks sinks;
};
