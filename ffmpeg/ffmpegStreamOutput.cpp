#include "ffmpegStreamOutput.h"
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
OutputStream::OutputStream(IAVFrameSink *buf) : src(buf), encoder_opts(NULL)
{}

OutputStream::~OutputStream()
{
  av_dict_free(&encoder_opts);
}

bool OutputStream::ready() { return ctx && src; }

AVStream *OutputStream::open()
{
  // if codec already open, close first
  if (ctx)
    close();

}

IAVFrameSource *OutputStream::setgetBuffer(IAVFrameSource *other_buf) { std::swap(sink, other_buf); return other_buf; }
void OutputStream::swapBuffer(IAVFrameSource *&other_buf) { std::swap(sink, other_buf); }
void OutputStream::setBuffer(IAVFrameSource *new_buf) { sink = new_buf; }
IAVFrameSource *OutputStream::getBuffer() const { return sink; }
IAVFrameSource *OutputStream::releaseBuffer() { IAVFrameSource *rval = sink; sink = NULL; return rval; }

AVPixelFormat OutputSteram::getPixelFormat() const { ctx?ctx->pix_fmt:AV_PIX_FMT_NONE; }

int OutputStream::processFrame(AVPacket *packet)
{
  int ret = 0; // FFmpeg return error code
  // AVFrame *frame = NULL;

  // // send packet to the decoder
  // if (packet)
  //   ret = avcodec_send_packet(ctx, packet);

  // // receive all the frames (could be more than one)
  // while (ret >= 0)
  // {
  //   ret = avcodec_receive_frame(ctx, frame);

  //   // if end-of-file, let sink know it
  //   if (ret == AVERROR_EOF)
  //   {
  //     if (sink)
  //       sink->push(NULL);
  //   }
  //   else if (ret >= 0)
  //   {
  //     pts = frame->pts = frame->best_effort_timestamp;
  //     if (sink && frame->pts >= buf_start_ts)
  //       sink->push(frame);
  //   }
  // }

  // if (ret==AVERROR_EOF || ret==AVERROR(EAGAIN))
  //   ret = 0;
  
  return ret;
}

