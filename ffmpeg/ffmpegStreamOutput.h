#pragma once

#include "ffmpegStream.h"
// #include "ffmpegAvRedefine.h"
#include "ffmpegAVFrameBufferInterfaces.h"

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
class OutputStream : public BaseStream
{
public:
  OutputStream(IAVFrameSource *buf = NULL);
  virtual ~OutputStream();

  virtual bool ready();

  virtual AVStream * open();
  virtual void close() {}

  IAVFrameSource *setgetBuffer(IAVFrameSource *other_buf);
  void swapBuffer(IAVFrameSource *&other_buf);
  void setBuffer(IAVFrameSource *new_buf);
  IAVFrameSource *getBuffer() const;
  IAVFrameSource *releaseBuffer();

  virtual int reset() { return 0; } // reset decoder states
  virtual int OutputStream::processFrame(AVPacket *packet);

protected:
  IAVFrameSource *src;
  AVDictionary *encoder_opts;
};

typedef std::vector<OutputStream*> OutputStreamPtrs;

class OutputVideoStream : public OutputStream
{
public:
  OutputVideoStream(IAVFrameSource *buf = NULL);
  virtual ~OutputVideoStream();

  AVStream *open() { return NULL; }

  AVPixelFormats getPixelFormats() const;
  AVPixelFormat getPixelFormat() const;

  AVPixelFormat choose_pixel_fmt(AVPixelFormat target) const;
  AVPixelFormats choose_pix_fmts() const;
private:
  bool keep_pix_fmt;
};

class OutputAudioStream : public OutputStream
{
public:
  OutputAudioStream(IAVFrameSource *buf = NULL) {}
  virtual ~OutputAudioStream() {}
  AVStream *open() { return NULL; }
private:
};
}
