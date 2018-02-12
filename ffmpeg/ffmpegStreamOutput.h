#pragma once

#include "ffmpegStream.h"
// #include "ffmpegAvRedefine.h"
#include "ffmpegAVFrameBufferInterfaces.h"
#include "ffmpegMediaStructs.h"

extern "C" {
// #include <libavformat/avformat.h>
// #include <libavcodec/avcodec.h>
// #include <libavutil/pixdesc.h>
}

namespace ffmpeg
{

/**
 * \brief Class to manage AVStream
 */
class OutputStream : virtual public BaseStream
{
public:
  OutputStream(IAVFrameSource *buf);
  virtual ~OutputStream();

  virtual bool ready();

  virtual AVStream *open();
  virtual void close() {}

  IAVFrameSource *setgetBuffer(IAVFrameSource *other_buf);
  void swapBuffer(IAVFrameSource *&other_buf);
  void setBuffer(IAVFrameSource *new_buf);
  IAVFrameSource *getBuffer() const;
  IAVFrameSource *releaseBuffer();

  virtual int reset()
  {
    avcodec_flush_buffers(ctx);
    return 0;
  } // reset decoder states
  virtual int OutputStream::processFrame(AVPacket *packet);

protected:
  IAVFrameSource *src;
  AVDictionary *encoder_opts;
};

typedef std::vector<OutputStream *> OutputStreamPtrs;

class OutputVideoStream : public VideoStream, public OutputStream
{
public:
  OutputVideoStream(IAVFrameSource *buf = NULL);
  virtual ~OutputVideoStream();

  AVPixelFormats getPixelFormats() const;
  AVPixelFormat choose_pixel_fmt(AVPixelFormat target) const;
  AVPixelFormats choose_pix_fmts() const;

private:
  bool keep_pix_fmt;
};

class OutputAudioStream : public AudioStream, public OutputStream
{
public:
  OutputAudioStream(IAVFrameSource *buf = NULL) : OutputStream(buf) {}
  virtual ~OutputAudioStream() {}

private:
};
}
