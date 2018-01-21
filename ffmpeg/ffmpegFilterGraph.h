#pragma once

#include "ffmpegBase.h"
// #include "mexClassHandler.h"
// #include "ffmpegPtrs.h"
#include "ffmpegAvRedefine.h"
// #include "ffmpegAVFramePtrBuffer.h"
#include "ffmpegFrameBuffers.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavutil/pixdesc.h>
}

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace ffmpeg
{

class FilterGraph : public Base
{
public:
  FilterGraph( const std::string &filtdesc = "", const AVPixelFormat pix_fmt = AV_PIX_FMT_NONE);
  ~FilterGraph();

  bool isFileOpen();
  bool atEndOfFile() { return filter_status == IDLE; }

  AVRational getSAR()
  {
    return fmt_ctx ? av_guess_sample_aspect_ratio(fmt_ctx, st, firstframe) : AVRational({0, 1});
  }

  double getDuration() const;

  int getBitsPerPixel() const
  {
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(pix_fmt);
    if (pix_desc == NULL)
      return -1;
    return av_get_bits_per_pixel(pix_desc);
  }

  uint64_t getNumberOfFrames() const
  {
    return (uint64_t)(getDuration() * getFrameRate());
  }

  double getFrameRate() const;

  double getCurrentTimeStamp() const;

  const std::string &getFilterGraph() const { return filter_descr; } // stops
  void setFilterGraph(const std::string &filter_desc, const AVPixelFormat pix_fmt = AV_PIX_FMT_NONE); // stops 

  const AVPixelFormat getPixelFormat() const { return pix_fmt; };
  const AVPixFmtDescriptor &getPixFmtDescriptor() const;
  size_t getNbPlanar() const
  {
    return av_pix_fmt_count_planes(pix_fmt);
  }
  size_t getNbPixelComponents() const
  {
    return getPixFmtDescriptor().nb_components;
  }

  size_t getWidth() const { return (firstframe) ? firstframe->width : 0; }
  size_t getHeight() const { return (firstframe) ? firstframe->height : 0; }
  size_t getFrameSize() const { return getWidth() * getHeight() * getNbPixelComponents(); }
  size_t getCurrentFrameCount()
  {
    std::unique_lock<std::mutex> buffer_guard(buffer_lock);
    return buf->size();
  };

  void resetBuffer(FrameBuffer *new_buf);
  FrameBuffer* releaseBuffer();
  size_t blockTillFrameAvail(size_t min_cnt = 1);
  size_t blockTillBufferFull();

private:
  void create_filters(const std::string &descr="", const AVPixelFormat pix_fmt = AV_PIX_FMT_NONE);
  void destroy_filters();

  void start();
  void pause();
  void resume();
  void stop();

  AVFormatContext *fmt_ctx;
  AVCodecContext *dec_ctx;
  AVFilterGraph *filter_graph;
  AVFilterContext *buffersrc_ctx;
  AVFilterContext *buffersink_ctx;

  int video_stream_index;
  AVStream *st; // selected stream

  AVPixelFormat pix_fmt;
  std::string filter_descr;

  std::atomic<int64_t> pts; // presentation timestamp of the last buffered

  AVRational tb; // timebase
  AVFrame *firstframe;
  std::mutex firstframe_lock;
  std::condition_variable firstframe_ready;

  FrameBuffer *buf;
  int64_t buf_start_ts; // if non-zero, frames with pts less than this number are ignored, used to seek to exact pts
  
  bool killnow; // true to kill member threads
  std::mutex buffer_lock;
  std::condition_variable buffer_ready;

  enum THREAD_STATUS
  {
    FAILED = -1, //
    IDLE,
    ACTIVE,
    PAUSE_RQ, // requested to stop reading and flush the pipeline
    INACTIVE  // state after last frame is processed until entering IDLE state
  };     // non-zero to idle
  std::atomic<THREAD_STATUS> filter_status;

  std::condition_variable buffer_flushed;

  // THREAD 2: responsible to read decoded frame and send it to ffMPEG filter graph
  void filter_frames();
  std::thread frame_filter; // read packets and send it to decoder

  std::exception_ptr eptr;

  void copy_frame_ts(const AVFrame *frame); // thread-safe copy frame to buffer
};
}
