#pragma once

#include "..\Common\mexClassHandler.h"
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
  static std::string get_componentid() { return "mexVideoReader"; }

  bool action_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);
  static bool static_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);

protected:
  void set_prop(const std::string name, const mxArray *value);
  mxArray *get_prop(const std::string name);

  void readFrame(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);             //    varargout = readFrame(obj, varargin);
  void read(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);                  //varargout = read(obj, varargin);
  static void getFileFormats(int nlhs, mxArray *plhs[]); // formats = getFileFormats();

  void readBuffer(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);             //    [frames,timestamps] = readFrame(obj);
private:
  std::string pix_fmt_name;
  ffmpeg::VideoReader reader;

  size_t nb_planar; // number of planar components
  size_t pix_byte; // number of bytes per component pixel
  size_t buffer_capacity; // in frames

  struct FrameBuffer
  {
    enum {FILLING, FILLED, READING, DONE } state;
    size_t cnt;
    size_t rd_pos;
    double *time;
    uint8_t *frame; // mxMalloc allocated
    uint8_t *planes[8]; // top of each planar data
    FrameBuffer() : state(DONE), cnt(0), rd_pos(0), time(NULL), frame(NULL) {}
    FrameBuffer(const size_t pix_byte, const size_t nb_planar, const size_t capacity) : FrameBuffer() { reset(pix_byte, nb_planar, capacity); }
    ~FrameBuffer();
    void reset(const size_t pix_byte, const size_t nb_planar, const size_t capacity);
  };
  typedef std::vector<FrameBuffer> FrameBufferVector;
  FrameBufferVector buffers;
  FrameBufferVector::iterator rd_buf, wr_buf;

  std::atomic<bool> killnow;
  std::thread frame_reader; // thread to swapping buffers 
  std::mutex buffer_lock;
  std::condition_variable buffer_ready;

  void stuff_buffer();

  static std::string mex_get_filterdesc(const mxArray *prhs);
  static AVPixelFormat mex_get_pixfmt(const std::string &pix_fmt_str);
  static size_t mex_get_numplanes();
};
