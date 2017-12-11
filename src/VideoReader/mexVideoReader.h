#pragma once

#include "..\Common\mexClassHandler.h"
#include "..\Common\mexAllocator.h"
#include "..\Common\ffmpegFrameBuffers.h"

#include "ffmpegVideoReader.h"

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

typedef std::vector<uint8_t> uint8_vector;

class mexVideoReader : public mexFunctionClass
{
public:
  mexVideoReader(int nrhs, const mxArray *prhs[]);
  ~mexVideoReader();
  static std::string get_componentid() { return "mexVideoReader"; }

  bool action_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);
  static bool static_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);

protected:
  void set_prop(const std::string name, const mxArray *value);
  mxArray *get_prop(const std::string name);

  void readFrame(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);             //    varargout = readFrame(obj, varargin);
  void read(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);                  //varargout = read(obj, varargin);
  void readBuffer(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);             //    [frames,timestamps] = readFrame(obj);

  static void getFileFormats(int nlhs, mxArray *plhs[]); // formats = getFileFormats();
  static void getVideoFormats(int nlhs, mxArray *plhs[]); // formats = getVideoFormats();
  static void getVideoCompressions(int nlhs, mxArray *plhs[]); // formats = getVideoCompressions();

private:
  ffmpeg::VideoReader reader;

  size_t nb_components;   // number of components
  size_t buffer_capacity; // in frames

  typedef ffmpeg::ComponentBufferBDReader<mexAllocator<uint8_t>> mexComponentBuffer;
  typedef std::vector<mexComponentBuffer,mexAllocator<mexComponentBuffer>> FrameBufferVector;
  FrameBufferVector buffers;
  FrameBufferVector::iterator rd_buf, wr_buf;

  std::atomic<bool> killnow;
  std::thread frame_writer; // thread to swapping buffers 
  std::mutex buffer_lock;
  std::condition_variable buffer_ready;

  void shuffle_buffers();

  static std::string mex_get_filterdesc(const mxArray *obj);
  static AVPixelFormat mex_get_pixfmt(const mxArray *obj);
  static size_t mex_get_numplanes();
};
