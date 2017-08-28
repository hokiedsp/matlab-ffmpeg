#pragma once

#include "mexClassHandler.h"
#include "ffmpegInputFileSelectStream.h"

class mexVideoReader : public mexFunctionClass
{
public:
  mexVideoReader(int nrhs, const mxArray *prhs[]);
  static std::string get_componentid() { return "mexVideoReader"; }

  bool action_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);
  static bool static_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);

protected:
  void set_prop(const std::string name, const mxArray *value);
  mxArray *get_prop(const std::string name);

  void readFrame(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);             //    varargout = readFrame(obj, varargin);
  void hasFrame(int nlhs, mxArray *plhs[]);              //        eof = hasFrame(obj);
  void read(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);                  //varargout = read(obj, varargin);
  static void getFileFormats(int nlhs, mxArray *plhs[]); // formats = getFileFormats();
private:
  ffmpeg::InputFileSelectStream reader;
};
