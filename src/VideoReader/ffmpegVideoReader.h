#include "../Common/ffmpegBase.h"
#include "../Common/mexClassHandler.h"
// #include "../Common/ffmpegPtrs.h"
#include "../Common/ffmpegAvRedefine.h"
// #include "ffmpegAVFramePtrBuffer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
}

#include <thread>
#include <mutex>
#include <condition_variable>

namespace ffmpeg
{
class VideoReader : public Base
{
public:
  VideoReader();
  ~VideoReader();

  void open_input_file(const std::string &filename);
  void close_input_file();

  void init_filters(const std::string &descr, const AVPixelFormat pix_fmt=AV_PIX_FMT_NONE);

  void start();
  void pause();
  void is_reading();

  void read_frames(bool block);

private:
  int video_stream_index;
  std::string filter_descr;

  AVFormatContext *fmt_ctx;
  AVCodecContext *dec_ctx;
  AVFilterContext *buffersink_ctx;
  AVFilterContext *buffersrc_ctx;
  AVFilterGraph *filter_graph;

  size_t buf_size;
  uint8_t *frame_buf = NULL;
  double *time_buf = NULL;
  
  bool killnow;
  enum STATUS {
    FAILED = -1,// 
    INIT = 0,
    IDLE,
    ACTIVE,
    FLUSH, // reached the end of file
    WAIT
  }; // non-zero to idle

  // THREAD 1: responsible to read packet and send it to ffMPEG decoder
  void read_packets();
  std::thread packet_reader; // read packets and send it to decoder
  std::mutex lock_reader;
  std::condition_variable reader_start;
  STATUS reader_status;

  // THREAD 2: responsible to read decoded frame and send it to ffMPEG filter graph
  void filter_frames();
  std::thread frame_filter; // read packets and send it to decoder
  std::mutex lock_filter;
  std::condition_variable filter_start;
  STATUS filter_status;

  // THREAD 3: responsible to read filtered frame to user specified buffer
  void buffer_frames();
  std::thread frame_output; // read packets and send it to decoder
  std::mutex lock_outputter;
  std::condition_variable outputter_start;
  STATUS buffer_status;

  std::exception_ptr eptr;

  void copy_frame(const AVFrame *frame, AVRational time_base);
};
}
