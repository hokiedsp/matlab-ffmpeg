#include "../Common/ffmpegBase.h"
// #include "../Common/mexClassHandler.h"
// #include "../Common/ffmpegPtrs.h"
#include "../Common/ffmpegAvRedefine.h"
// #include "ffmpegAVFramePtrBuffer.h"

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
    return fmt_ctx ? av_guess_sample_aspect_ratio(fmt_ctx, st, firstframe) : AVRational({0, 0});
  }

  double getDuration() const
  {
    if (!fmt_ctx)
      return NAN;

    // defined in us in the format context
    double secs = NAN;
    if (fmt_ctx->duration != AV_NOPTS_VALUE)
    {
      int64_t duration = fmt_ctx->duration + (fmt_ctx->duration <= INT64_MAX - 5000 ? 5000 : 0);
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

  double getFrameRate() const
  {
    return (fmt_ctx) ? (double(st->avg_frame_rate.num) / st->avg_frame_rate.den) : NAN;
  }

  std::string getCodecName() const
  {
    return (fmt_ctx) ? fmt_ctx->video_codec->name : "";
  }

  std::string getCodecDescription() const
  {
    return (fmt_ctx && fmt_ctx->video_codec->long_name) ? fmt_ctx->video_codec->long_name : "";
  }

  double getCurrentTimeStamp() const
  {
    return (fmt_ctx) ? (double(pts / 100) / (AV_TIME_BASE / 100)) : NAN;
  }
  void setCurrentTimeStamp(const double val)
  {
    // TBD
  }

  const AVPixFmtDescriptor &getPixFmtDescriptor() const;
  size_t getNbPlanar() const
  {
    return av_pix_fmt_count_planes(pix_fmt);
  }
  size_t getNbPixelComponents() const
  {
    const AVPixFmtDescriptor &pfd = getPixFmtDescriptor();
    return (pfd.flags & AV_PIX_FMT_FLAG_PLANAR) ? 1 : pfd.nb_components;
  }

  size_t getWidth() const { return (dec_ctx) ? dec_ctx->width : 0; }
  size_t getHeight() const { return (dec_ctx) ? dec_ctx->height : 0; }
  size_t getFrameSize() const { return getWidth() * getHeight() * getNbPixelComponents(); }
  size_t getCurrentFrameCount()
  {
    std::unique_lock<std::mutex> buffer_guard(buffer_lock);
    return buf_count;
  };

  size_t resetBuffer(size_t sz, uint8_t *frame[], double *time = NULL);
  size_t releaseBuffer();
  size_t blockTillFrameAvail(size_t min_cnt = 1);
  size_t blockTillBufferFull();

private:
  void open_input_file(const std::string &filename);
  void close_input_file(); // must call stop() before calling this function

  void create_filters(const std::string &descr, const AVPixelFormat pix_fmt = AV_PIX_FMT_NONE);
  void destroy_filters();

  void start();

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

  std::atomic<uint64_t> pts; // presentation timestamp of the last buffered
  std::atomic<bool> eof;

  AVFrame *firstframe;
  std::mutex firstframe_lock;
  std::condition_variable firstframe_ready;

  size_t buf_size;
  uint8_t **frame_buf;
  double *time_buf;
  std::atomic<size_t> buf_count;

  std::mutex reader_lock;
  std::condition_variable reader_ready;
  std::mutex decoder_input_lock;
  std::condition_variable decoder_input_ready;
  std::mutex decoder_output_lock;
  std::condition_variable decoder_output_ready;
  std::mutex filter_input_lock;
  std::condition_variable filter_input_ready;
  std::mutex filter_output_lock;
  std::condition_variable filter_output_ready;
  std::mutex buffer_lock;
  std::condition_variable buffer_ready;

  enum STATUS
  {
    FAILED = -1, //
    IDLE,
    ACTIVE,
    STOP // requested to stop reading
  };     // non-zero to idle

  bool killnow;
  std::atomic<STATUS> reader_status;
  std::atomic<bool> paused;

  // THREAD 1: responsible to read packet and send it to ffMPEG decoder
  void read_packets();
  std::thread packet_reader; // read packets and send it to decoder

  // THREAD 2: responsible to read decoded frame and send it to ffMPEG filter graph
  void filter_frames();
  std::thread frame_filter; // read packets and send it to decoder

  // THREAD 3: responsible to read filtered frame to user specified buffer
  void buffer_frames();
  std::thread frame_output; // read packets and send it to decoder

  std::exception_ptr eptr;

  void copy_frame_ts(const AVFrame *frame, AVRational time_base); // thread-safe copy frame to buffer
  int copy_frame(const AVFrame *frame, AVRational time_base);     // copy frame to buffer
};
}
