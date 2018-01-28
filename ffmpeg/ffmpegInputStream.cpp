#include "ffmpegInputStream.h"
#include "ffmpegException.h"

extern "C" {
#include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

using namespace ffmpeg;

/**
 * \brief Class to manage AVStream
 */
InputStream::InputStream(AVStream *s, IAVFrameSink *buf) : st(NULL), ctx(NULL), sink(buf), buf_start_ts(0), pts(0)
{
  if (st) open(st);
}

InputStream::~InputStream()
{
  if (ctx) close();
}

bool InputStream::ready() { return ctx && sink; }

void InputStream::open(AVStream *s)
{
  // if codec already open, close first
  if (ctx)
    close();

  AVCodecParameters *par = s->codecpar;
  AVCodec *dec = avcodec_find_decoder(par->codec_id);
  if (!dec)
    throw ffmpegException("Failed to find a codec");

  // create decoding context if not already created
  AVCodecContext *dec_ctx = avcodec_alloc_context3(dec);
  if (!dec_ctx)
    throw ffmpegException("Failed to allocate a decoder context");

  avcodec_parameters_to_context(dec_ctx, par);
  av_opt_set_int(dec_ctx, "refcounted_frames", 1, 0);

  /* open the codec */
  if (avcodec_open2(dec_ctx, dec, NULL) < 0)
    throw ffmpegException("Cannot open the decoder");

  // all successful, update member variables
  st = s;
  ctx = dec_ctx;
  st->discard = AVDISCARD_NONE;
}

void InputStream::close()
{
  // if no stream is associated, nothing to do
  if (!ctx) return;

  // free up the context
  avcodec_free_context(&ctx);

  st->discard = AVDISCARD_ALL;
  st = NULL;
  ctx = NULL;
}

IAVFrameSink *InputStream::setgetBuffer(IAVFrameSink *other_buf) { std::swap(sink, other_buf); return other_buf; }
void InputStream::swapBuffer(IAVFrameSink *&other_buf) { std::swap(sink, other_buf); }
void InputStream::setBuffer(IAVFrameSink *new_buf) { sink = new_buf; }
IAVFrameSink *InputStream::getBuffer() const { return sink; }
IAVFrameSink *InputStream::releaseBuffer() { IAVFrameSink *rval = sink; sink = NULL; return rval; }

void InputStream::setStartTime(const int64_t timestamp) { buf_start_ts = timestamp; }
        
int InputStream::reset()
{
  return avcodec_send_packet(ctx, NULL);
}

int InputStream::processPacket(AVPacket *packet)
{
  int ret; // FFmpeg return error code
  AVFrame *frame = NULL;

  // send packet to the decoder
  if (packet)
    ret = avcodec_send_packet(ctx, packet);

  // receive all the frames (could be more than one)
  while (ret >= 0)
  {
    ret = avcodec_receive_frame(ctx, frame);

    // if end-of-file, let sink know it
    if (ret == AVERROR_EOF)
    {
      if (sink)
        sink->push(NULL);
    }
    else if (ret >= 0)
    {
      pts = frame->pts = frame->best_effort_timestamp;
      if (sink && frame->pts >= buf_start_ts)
        sink->push(frame);
    }
  }

  if (ret==AVERROR_EOF || ret==AVERROR(EAGAIN))
    ret = 0;
  
  return ret;
}

///////////////////////////////////////////////////////

AVStream *InputStream::getAVStream() const { return st; }
int InputStream::getId() const { return st?st->index : -1; }

std::string InputStream::getCodecName() const
{
  return (ctx && ctx->codec && ctx->codec->name) ? ctx->codec->name : "";
}
std::string InputStream::getCodecDescription() const
{
  return (ctx && ctx->codec && ctx->codec->long_name) ? ctx->codec->long_name : "";
}

AVRational InputStream::getTimeBase() const { return (st)?st->time_base:AVRational({0,0}); }
int64_t InputStream::getLastFrameTimeStamp() const { return pts; }
