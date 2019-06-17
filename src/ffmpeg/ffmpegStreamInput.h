#pragma once

#include "ffmpegStream.h"
// #include "ffmpegAvRedefine.h"
#include "ffmpegAVFrameEndpointInterfaces.h"
#include "ffmpegAVFrameBufferInterfaces.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
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
    if (sink)
      return *sink;
    throw ffmpegException("No buffer.");
  }
  void setSinkBuffer(IAVFrameSinkBuffer &buf)
  {
    if (sink)
      sink->clrSrc();
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
  void setStartTime(const int64_t timestamp); // ignores all frames before this time

protected:
  InputFormat *reader;
  AVFrame *frame;
  IAVFrameSinkBuffer *sink;
  int64_t buf_start_ts; // if non-zero, frames with pts less than this number are ignored, used to seek to exact pts
};

typedef std::vector<InputStream *> InputStreamPtrs;

class InputVideoStream : public VideoStream, public InputStream
{
public:
  InputVideoStream(InputFormat &reader, int stream_id, IAVFrameSinkBuffer &buf) : InputStream(reader, stream_id, buf) {}
  virtual ~InputVideoStream() {}

  AVRational getAvgFrameRate() const { return st ? st->avg_frame_rate : AVRational({0, 0}); }

  void setPixelFormat(const AVPixelFormat pix_fmt);

  // int getBitsPerPixel() const;

  // const AVPixFmtDescriptor *getPixFmtDescriptor() const;
  // size_t getNbPixelComponents() const;

  // size_t getFrameSize() const;
private:
};

class InputAudioStream : public AudioStream, public InputStream
{
public:
  InputAudioStream(InputFormat &reader, int stream_id, IAVFrameSinkBuffer &buf);
  virtual ~InputAudioStream();

  void open(AVStream *st);
  void close();

private:
};
} // namespace ffmpeg
