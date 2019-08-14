#pragma once

#include <mexAllocator.h>
#include <mexObjectHandler.h>

#include "../../ffmpeg/ffmpegAVFrameQueue.h"
#include "../../ffmpeg/ffmpegReaderMT.h"
// #include "../../ffmpeg/filter/ffmpegFilterGraph.h"
#include "mexReaderPostOps.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
// #include <thread>
// #include <mutex>
// #include <condition_variable>

typedef std::vector<uint8_t> uint8_vector;
// typedef ffmpeg::AVFrameQueue<NullMutex, NullConditionVariable<NullMutex>,
//                              NullUniqueLock<NullMutex>>
//     AVFrameBuffer;
typedef ffmpeg::AVFrameDoubleBufferMT AVFrameBuffer;

// typedef ffmpeg::AVFrameQueue<Cpp11Mutex,
//                              Cpp11ConditionVariable<Cpp11Mutex,
//                              Cpp11UniqueLock<Cpp11Mutex>>,
//                              Cpp11UniqueLock<Cpp11Mutex>>
//     AVFrameBuffer;

// typedef ffmpeg::Reader<AVFrameQueue> ffmpegReader;
typedef ffmpeg::ReaderMT ffmpegReader;

class mexFFmpegReader
{
  public:
  typedef std::chrono::duration<double> mex_duration_t;

  mexFFmpegReader(const mxArray *mxObj, int nrhs, const mxArray *prhs[]);
  ~mexFFmpegReader();
  static std::string get_classname()
  {
    return "ffmpeg.Reader"; // associated matlab class name
  }
  bool action_handler(const mxArray *mxObj, const std::string &command,
                      int nlhs, mxArray *plhs[], int nrhs,
                      const mxArray *prhs[]);
  static bool static_handler(const std::string &command, int nlhs,
                             mxArray *plhs[], int nrhs, const mxArray *prhs[]);

  private:
  // "quasi-public" as directly called by action_handler or static_handler
  void activate(mxArray *mxObj);

  template <typename Spec>
  int add_stream(const mxArray *mxObj, const Spec &spec)
  {
    // the first stream gets the finite buffer size while the secondary streams
    // are dynamically buffered
    if (streams.empty())
    {
      int N = (int)mxGetScalar(mxGetProperty(mxObj, 0, "BufferSize"));
      auto ret = reader.addStream(spec, -1, N);
      reader.setPrimaryStream(spec);
      return ret;
    }
    else
      return reader.addStream(spec);
  }

  mxArray *hasFrame();
  mxArray *hasMediaType(const AVMediaType type);

  //    varargout = readFrame(obj, varargin);
  void readFrame(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);
  void read(int nlhs, mxArray *plhs[], int nrhs,
            const mxArray *prhs[]); // varargout = read(obj, varargin);
  void setCurrentTime(const mxArray *mxTime);
  mxArray *getCurrentTime();

  static mxArray *
  mxCreateFileFormatName(AVPixelFormat fmt); // formats = getFileFormats();

  static mxArray *getFileFormats();  // formats = getFileFormats();
  static mxArray *getVideoFormats(); // formats = getVideoFormats();

  ffmpegReader reader;

  std::string filt_desc; // actual filter graph description

  std::vector<std::string> streams; /// names of active video streams
  std::unordered_map<std::string, mexFFmpegVideoPostOp>
      postfilts; // post-process video filters

  /**
   * \brief Setup filter graph & streams according to the Matlab class object
   *        properties
   */
  void set_streams(const mxArray *mxObj);

  /**
   * \brief Add post-filters to the active streams to make them ready to be
   * exported to MATLAB
   */
  void set_postops(mxArray *mxObj);

  /**
   * \brief Read the next primary stream
   *
   * \returns mxArray containing the received frame
   */
  mxArray *read_frame();

  /**
   * \brief Read the next frame(s) of the specified secondary stream
   *
   * \param[in]  spec   Name of the stream to retrieve (must not be the primary
   * spec, unchecked) \returns mxArray containing the received frame(s).
   */
  mxArray *read_frame(const std::string &spec);

  mxArray *read_video_frame(size_t nframes);
  mxArray *read_audio_frame(size_t nframes);

  // temp frame storage & management
  std::vector<AVFrame *> frames;
  void add_frame();
  class purge_frames
  {
public:
    purge_frames(std::vector<AVFrame *> &frms) : frames(frms), nfrms(1) {}
    purge_frames(std::vector<AVFrame *> &frms, size_t n)
        : frames(frms), nfrms(n)
    {
    }
    ~purge_frames()
    {
      for (auto &f : frames) av_frame_unref(f);
    }
    size_t nfrms;

private:
    std::vector<AVFrame *> &frames;
  };
};

// inlines & template member function implementations
inline void mexFFmpegReader::add_frame()
{
  AVFrame *frame = av_frame_alloc();
  if (!frame)
    mexErrMsgIdAndTxt("ffmpeg:Reader:NoMemory",
                      "Failed to allocate memory for an AVFrame.");
  frames.push_back(frame);
}
