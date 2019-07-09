#include "ffmpegStreamInput.h"
#include "ffmpegException.h"
#include "ffmpegFormatInput.h"

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
InputStream::InputStream(InputFormat &rdr, int stream_id,
                         IAVFrameSinkBuffer &buf)
    : reader(&rdr), sink(&buf)
{
  frame = av_frame_alloc();
  open(reader->_get_stream(stream_id));
  buf.setSrc(*this);
}

InputStream::~InputStream() { av_frame_free(&frame); }

void InputStream::open(AVStream *s)
{
  // do nothing if given null
  if (!s) return;

  // if codec already open, close first
  if (ctx) close();

  AVCodecParameters *par = s->codecpar;
  AVCodec *dec = avcodec_find_decoder(par->codec_id);
  if (!dec) throw Exception("Failed to find a codec");

  // create decoding context if not already created
  AVCodecContext *dec_ctx = avcodec_alloc_context3(dec);
  if (!dec_ctx) throw Exception("Failed to allocate a decoder context");

  avcodec_parameters_to_context(dec_ctx, par);
  av_opt_set_int(dec_ctx, "refcounted_frames", 1, 0);

  /* open the codec */
  if (avcodec_open2(dec_ctx, dec, NULL) < 0)
    throw Exception("Cannot open the decoder");

  // all successful, update member variables
  st = s;
  ctx = dec_ctx;
  st->discard = AVDISCARD_NONE;
}

int InputStream::processPacket(AVPacket *packet)
{
  // send packet to the decoder (if not eos)
  int ret = avcodec_send_packet(ctx, packet);

  // receive all the frames (could be more than one)
  while (!ret || ret == AVERROR_EOF)
  {
    ret = avcodec_receive_frame(ctx, frame);

    // if end-of-file, let sink know it
    if (sink)
    {
      if (ret == AVERROR_EOF)
      {
        sink->push(NULL);
        return 0; // EOF is a valid outcome
      }
      else if (ret >= 0)
        sink->push(frame);
    }
  }

  // EAGAIN is a valid outcome
  return (ret == AVERROR(EAGAIN)) ? 0 : ret;
}

///////////////////////////////////////////////////////////////////////////////

void InputVideoStream::setPixelFormat(const AVPixelFormat pix_fmt)
{
  if (pix_fmt == AV_PIX_FMT_NONE) return;
  if (av_opt_set_pixel_fmt(ctx, "pix_fmt", pix_fmt, 0) > 0)
    throw Exception("Invalid pixel format specified.");
}
