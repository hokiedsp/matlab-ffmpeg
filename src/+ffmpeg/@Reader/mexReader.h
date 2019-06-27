#pragma once

#include "../../ffmpeg/ffmpegAVFrameQueue.h"
#include "../../ffmpeg/ffmpegReader.h"
// #include "../../ffmpeg/filter/ffmpegFilterGraph.h"
#include "../../ffmpeg/syncpolicies.h"

// #include "ffmpeg/ffmpegAVFrameQueue.h" // to pass frames from decoder to
// filtergraph #include "ffmpeg/ffmpegAVFrameVideoComponentSink.h" // frame
// buffer

#include <mexAllocator.h>
#include <mexObjectHandler.h>

#include <string>
#include <unordered_map>
#include <vector>
// #include <thread>
// #include <mutex>
// #include <condition_variable>

typedef std::vector<uint8_t> uint8_vector;
typedef ffmpeg::AVFrameQueue<NullMutex, NullConditionVariable<NullMutex>,
                             NullUniqueLock<NullMutex>>
    AVFrameQueue;
// typedef ffmpeg::AVFrameQueue<Cpp11Mutex,
//                              Cpp11ConditionVariable<Cpp11Mutex,
//                              Cpp11UniqueLock<Cpp11Mutex>>,
//                              Cpp11UniqueLock<Cpp11Mutex>>
//     AVFrameQueue;

class mexFFmpegReader
{
  public:
  mexFFmpegReader(const mxArray *mxObj, int nrhs, const mxArray *prhs[]);
  ~mexFFmpegReader();
  static std::string get_classname()
  {
    return "ffmpeg.VideoReader"; // associated matlab class name
  }
  bool action_handler(const mxArray *mxObj, const std::string &command,
                      int nlhs, mxArray *plhs[], int nrhs,
                      const mxArray *prhs[]);
  static bool static_handler(const std::string &command, int nlhs,
                             mxArray *plhs[], int nrhs, const mxArray *prhs[]);

  private:
  // "quasi-public" as directly called by action_handler or static_handler
  void addStreams(const mxArray *mxObj);
  void activate(const mxArray *mxObj);
  bool hasFrame();
  void
  readFrame(int nlhs, mxArray *plhs[], int nrhs,
            const mxArray *prhs[]); //    varargout = readFrame(obj, varargin);
  void read(int nlhs, mxArray *plhs[], int nrhs,
            const mxArray *prhs[]); // varargout = read(obj, varargin);
  void setCurrentTime(double time, const bool reset_buffer = true);

  static mxArray *getFileFormats();  // formats = getFileFormats();
  static mxArray *getVideoFormats(); // formats = getVideoFormats();

  ffmpeg::Reader reader;

  double ts;

  std::string filt_desc; // actual filter graph description

  std::vector<std::string> streams; /// names of active video streams

  /**
   * \brief Read the next primary straem
   */
  double read_frame(mxArray *mxData);

  /**
   * \brief Read the next frame(s) of the specified secondary stream
   */
  void read_frame(mxArray *mxData, const std::string &spec);

  static std::string get_video_format_filter(const mxArray *mxObj);

  std::vector<AVFrame *> frames; // temp frame storage
  void add_frame()
  {
    AVFrame *frame = av_frame_alloc();
    if (!frame)
      mexErrMsgIdAndTxt("ffmpeg:Reader:NoMemory",
                        "Failed to allocate memory for an AVFrame.");
    frames.push_back(frame);
  }
};
