#pragma once

#include "ffmpegBase.h"
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
class InputStream : public Base
{
public:
  InputStream(AVStream *st = NULL, IAVFrameSink *buf = NULL);
  virtual ~InputStream();

  virtual bool ready();

  virtual void open(AVStream *st);
  virtual void close();

  IAVFrameSink *setgetBuffer(IAVFrameSink *other_buf);
  void swapBuffer(IAVFrameSink *&other_buf);
  void setBuffer(IAVFrameSink *new_buf);
  IAVFrameSink *getBuffer() const;
  IAVFrameSink *releaseBuffer();

  virtual int reset(); // reset decoder states
  virtual int processPacket(AVPacket *packet);
  void setStartTime(const int64_t timestamp); // ignores all frames before this time
  
  AVStream *getAVStream() const;
  int getId() const;

  AVCodec *getCodec() const;
  std::string getCodecName() const;
  std::string getCodecDescription() const;
  AVRational getTimeBase() const;
  int64_t getLastFrameTimeStamp() const;

protected:
  AVStream *st;        // stream
  AVCodecContext *ctx; // stream's codec context

  IAVFrameSink *sink;

  int64_t buf_start_ts; // if non-zero, frames with pts less than this number are ignored, used to seek to exact pts
  int64_t pts; // pts of the last frame 
};

class InputVideoStream : public InputStream
{
public:
  InputVideoStream(AVStream *st = NULL, IAVFrameSink *buf = NULL);
  virtual ~InputVideoStream();

  AVRational getAvgFrameRate() const;

  // AVRational getSAR() const;

  // int getBitsPerPixel() const;

  // const AVPixFmtDescriptor *getPixFmtDescriptor() const;
  // size_t getNbPixelComponents() const;

  // size_t getWidth() const;
  // size_t getHeight() const;
  // size_t getFrameSize() const;
};

}
