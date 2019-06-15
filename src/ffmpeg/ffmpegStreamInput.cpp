#include "ffmpegStreamInput.h"
#include "ffmpegException.h"
#include "ffmpegMediaReader.h"

extern "C"
{
#include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/rational.h>
}

using namespace ffmpeg;

/**
 * \brief Class to manage AVStream
 */
InputStream::InputStream(InputFormat &rdr, int stream_id, IAVFrameSinkBuffer &buf) : reader(&rdr), sink(&buf), buf_start_ts(0)
{
  AVStream *st = reader->_get_stream(stream_id);
  if (st) open(st);
}

InputStream::~InputStream()
{
}

void InputStream::open(AVStream *s)
{
  // do nothing if given null
  if (!s) return;

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

void InputStream::setStartTime(const int64_t timestamp) { buf_start_ts = timestamp; }

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
      pts = av_rescale_q(frame->best_effort_timestamp, st->time_base, AV_TIME_BASE_Q); // a * b / c
      if (sink && frame->pts >= buf_start_ts)
        sink->push(frame);
    }
  }

  if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
    ret = 0;

  return ret;
}
