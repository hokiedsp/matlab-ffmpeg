#pragma once

#include "ffmpegStream.h"
// #include "ffmpegAvRedefine.h"
#include "ffmpegAVFrameBufferInterfaces.h"
#include "ffmpegAVFrameEndpointInterfaces.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
  // #include <libavutil/pixdesc.h>
}

namespace ffmpeg
{

class InputFormat;

/**
 * \brief Class to manage AVStream
 */
class InputStream : virtual public BaseStream, public IAVFrameSource
{
  public:
  InputStream() = default; // only for the use with stl containers
  InputStream(InputFormat &reader, int stream_id, IAVFrameSinkBuffer &buf);
  virtual ~InputStream();

  virtual bool ready() { return ctx && sink; }

  virtual void open(AVStream *st);
  // virtual void close();

  using BaseStream::getTimeBase;

  // Implementing IAVFrameSource interface
  IAVFrameSinkBuffer &getSinkBuffer() const
  {
    if (sink) return *sink;
    throw Exception("No buffer.");
  }
  void setSinkBuffer(IAVFrameSinkBuffer &buf)
  {
    if (sink) sink->clrSrc();
    sink = &buf;
    sink->setSrc(*this);
  }
  void clrSinkBuffer()
  {
    if (sink)
    {
      sink->clrSrc();
      sink = NULL;
    }
  }
  // end Implementing IAVFrameSource interface

  // virtual int reset(); // reset decoder states
  virtual int processPacket(AVPacket *packet);

  protected:
  InputFormat *reader;
  AVFrame *frame;
  IAVFrameSinkBuffer *sink;
};

typedef std::vector<InputStream *> InputStreamPtrs;

class InputVideoStream : public InputStream, public VideoStream
{
  public:
  InputVideoStream(InputFormat &reader, int stream_id, IAVFrameSinkBuffer &buf)
      : InputStream(reader, stream_id, buf)
  {
    if (st) syncMediaParams();
  }
  virtual ~InputVideoStream() {}

  void open(AVStream *st)
  {
    InputStream::open(st);
    syncMediaParams();
  }

  AVRational getAvgFrameRate() const
  {
    return st ? st->avg_frame_rate : AVRational({0, 0});
  }

  void setPixelFormat(const AVPixelFormat pix_fmt);

  // int getBitsPerPixel() const;

  // const AVPixFmtDescriptor *getPixFmtDescriptor() const;
  // size_t getNbPixelComponents() const;

  // size_t getFrameSize() const;
  private:
};

class InputAudioStream : public InputStream, public AudioStream
{
  public:
  InputAudioStream(InputFormat &reader, int stream_id, IAVFrameSinkBuffer &buf)
      : InputStream(reader, stream_id, buf)
  {
    if (st) syncMediaParams();
  }
  virtual ~InputAudioStream() {}

  void open(AVStream *st)
  {
    InputStream::open(st);
    syncMediaParams();
  }

  private:
};
} // namespace ffmpeg
