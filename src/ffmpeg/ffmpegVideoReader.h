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

class VideoReader : public Base
{
public:
  VideoReader(const std::string &filename = "", const std::string &filtdesc = "", const AVPixelFormat pix_fmt = AV_PIX_FMT_NONE);
  ~VideoReader();

  bool isFileOpen();
  bool atEndOfFile() { return filter_status == IDLE; }

  void openFile(const std::string &filename, const std::string &filtdesc = "", const AVPixelFormat pix_fmt = AV_PIX_FMT_NONE)
  {
    open_input_file(filename);
    create_filters(filtdesc, pix_fmt);
    reader_status = ACTIVE;
    start();
  }

  void closeFile()
  {
    if (isFileOpen())
    {
      stop();
      destroy_filters();
      close_input_file();
    }
  }

  AVRational getSAR()
  {
    return fmt_ctx ? av_guess_sample_aspect_ratio(fmt_ctx, st, firstframe) : AVRational({0, 1});
  }

  double getDuration() const
  {
    if (!fmt_ctx)
      return NAN;

    // defined in us in the format context
    double secs = NAN;
    if (fmt_ctx->duration != AV_NOPTS_VALUE)
    {
      int64_t duration = fmt_ctx->duration;
      secs = double(duration / 100) / (AV_TIME_BASE / 100);
    }

    return secs;
  }

  int getBitsPerPixel() const
  {
    if (!fmt_ctx)
      return -1;

    AVPixelFormat f;
    if (filter_graph)
    {
      f = pix_fmt;
    }
    else
    {
      if (dec_ctx->pix_fmt == AV_PIX_FMT_NONE)
        return -1;
      f = dec_ctx->pix_fmt;
    }
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(f);
    if (pix_desc == NULL)
      return -1;
    return av_get_bits_per_pixel(pix_desc);
  }

  uint64_t getNumberOfFrames() const
  {
    return (uint64_t)(getDuration() * getFrameRate());
  }

  std::string getFilePath() const
  {
    return fmt_ctx ? fmt_ctx->filename : "";
  }

  double getFrameRate() const;

  std::string getCodecName() const
  {
    return (dec_ctx && dec_ctx->codec && dec_ctx->codec->name) ? dec_ctx->codec->name : "";
  }

  std::string getCodecDescription() const
  {
    return (dec_ctx && dec_ctx->codec && dec_ctx->codec->long_name) ? dec_ctx->codec->long_name : "";
  }

  double getCurrentTimeStamp() const
  {
    return (fmt_ctx) ? (double(av_rescale_q(pts, (filter_graph) ? buffersink_ctx->inputs[0]->time_base : st->time_base, AV_TIME_BASE_Q) / 100) / (AV_TIME_BASE / 100)) : NAN;
  }
  void setCurrentTimeStamp(const double val, const bool exact_search = true);

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
  void open_input_file(const std::string &filename);
  void close_input_file(); // must call stop() before calling this function

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
  std::mutex reader_lock;
  std::condition_variable reader_ready;
  std::mutex decoder_lock;
  std::condition_variable decoder_ready;
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
  std::atomic<THREAD_STATUS> reader_status;
  std::atomic<THREAD_STATUS> filter_status;

  std::condition_variable buffer_flushed;

  // THREAD 1: responsible to read packet and send it to ffMPEG decoder
  void read_packets();
  std::thread packet_reader; // read packets and send it to decoder

  // THREAD 2: responsible to read decoded frame and send it to ffMPEG filter graph
  void filter_frames();
  std::thread frame_filter; // read packets and send it to decoder

  std::exception_ptr eptr;

  void copy_frame_ts(const AVFrame *frame); // thread-safe copy frame to buffer
};
}
