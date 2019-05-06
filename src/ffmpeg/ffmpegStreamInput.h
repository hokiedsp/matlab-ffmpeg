#pragma once

#include "ffmpegStream.h"
// #include "ffmpegAvRedefine.h"
#include "ffmpegAVFrameBufferInterfaces.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
// #include <libavutil/pixdesc.h>
}

namespace ffmpeg
{

/**
 * \brief Class to manage AVStream
 */
class InputStream : virtual public BaseStream
{
public:
  InputStream(AVStream *st = NULL, IAVFrameSink *buf = NULL);
  virtual ~InputStream();

  virtual bool ready();

  virtual void open(AVStream *st);
  // virtual void close();

  IAVFrameSink *setgetBuffer(IAVFrameSink *other_buf);
  void swapBuffer(IAVFrameSink *&other_buf);
  void setBuffer(IAVFrameSink *new_buf);
  IAVFrameSink *getBuffer() const;
  IAVFrameSink *releaseBuffer();

  // virtual int reset(); // reset decoder states
  virtual int processPacket(AVPacket *packet);
  void setStartTime(const int64_t timestamp); // ignores all frames before this time

protected:
  IAVFrameSink *sink;
  int64_t buf_start_ts; // if non-zero, frames with pts less than this number are ignored, used to seek to exact pts
};

typedef std::vector<InputStream *> InputStreamPtrs;

class InputVideoStream : public VideoStream, public InputStream
{
public:
  InputVideoStream(AVStream *st = NULL, IAVFrameSink *buf = NULL);
  virtual ~InputVideoStream();

  AVRational getAvgFrameRate() const;

  // int getBitsPerPixel() const;

  // const AVPixFmtDescriptor *getPixFmtDescriptor() const;
  // size_t getNbPixelComponents() const;

  // size_t getFrameSize() const;
private:
};

class InputAudioStream : public AudioStream, public InputStream
{
public:
  InputAudioStream(AVStream *st = NULL, IAVFrameSink *buf = NULL);
  virtual ~InputAudioStream();

  void open(AVStream *st);
  void close();

private:
};
}
