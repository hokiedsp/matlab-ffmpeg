#pragma once

#include "ffmpegMediaReader.h"
#include "filter/ffmpegFilterGraph.h"

#include "ffmpegAVFrameQueue.h" // to pass frames from decoder to filtergraph
#include "ffmpegAVFrameVideoComponentSink.h" // frame buffer

#include <mexObjectHandler.h>
#include <mexAllocator.h>

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

typedef std::vector<uint8_t> uint8_vector;

class mexVideoReader
{
public:
  mexVideoReader(const mxArray *mxObj, int nrhs, const mxArray *prhs[]);
  ~mexVideoReader();
  static std::string get_classname() { return "ffmpeg.VideoReader"; } // associated matlab class
  bool action_handler(const mxArray *mxObj, const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);
  static bool static_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);

private:
  bool hasFrame();
  void readFrame(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);             //    varargout = readFrame(obj, varargin);
  void read(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);                  //varargout = read(obj, varargin);
  void readBuffer(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);             //    [frames,timestamps] = readFrame(obj);

  static mxArray *getFileFormats(); // formats = getFileFormats();
  static mxArray *getVideoFormats(); // formats = getVideoFormats();
  static mxArray *getVideoCompressions(); // formats = getVideoCompressions();
  static mxArray *getVideoCompressions();
  static AVPixelFormat mexVideoReader::mexArrayToFormat(const mxArray *obj);
  static void validateSARString(const mxArray *prhs); // tf = isValidSAR(SARexpr);
  static AVRational mexArrayToSAR(const mxArray *mxObj);

  void setCurrentTime(double time, const bool reset_buffer = true);

  void open_file(const mxArray*mxObj, const std::string &mxFilename);

  void shuffle_buffers(); // read forwards
  static std::string mex_get_filterdesc(const mxArray *obj);
  
  ffmpeg::VideoReader reader;
  ffmpeg::filter::Graph filtergraph;

  bool rd_rev;  // false to read forward, true to read reverse
  
  enum
  {
    ON,   // video frame buffering is in progress
    LAST, // working on the last buffer
    OFF   // state after last frame is processed until entering IDLE state
  } state; // reader's buffering state

  double rd_rev_t_last; // set when state=LAST

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

};
