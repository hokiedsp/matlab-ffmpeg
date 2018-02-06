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
class InputStream : public BaseStream
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

class InputVideoStream : public InputStream, public IVideoHandler
{
public:
  InputVideoStream(AVStream *st = NULL, IAVFrameSink *buf = NULL);
  virtual ~InputVideoStream();

  const VideoParams &getVideoParams() const
  {
    return vparams;
  }

  AVRational getAvgFrameRate() const;

  int getWidth() const;
  int getHeight() const;
  AVPixelFormat getPixelFormat() const;
  AVRational getSAR() const;

  // int getBitsPerPixel() const;

  // const AVPixFmtDescriptor *getPixFmtDescriptor() const;
  // size_t getNbPixelComponents() const;

  // size_t getFrameSize() const;
  void open(AVStream *st);
  void close();
private:
  BasicMediaParams bparams;
  VideoParams vparams;
};

class InputAudioStream : public InputStream, public IAudioHandler
{
public:
  InputAudioStream(AVStream *st = NULL, IAVFrameSink *buf = NULL);
  virtual ~InputAudioStream();

  const AudioParams &getAudioParams() const { return aparams; }

  AVSampleFormat getSampleFormat() const;
  int getChannels() const;
  uint64_t getChannelLayout() const;

  void open(AVStream *st);
  void close();
  // bparams = (st) ? BasicMediaParams({st->codecpar->codec_type, st->time_base}) : BasicMediaParams({AVMEDIA_TYPE_AUDIO, {0, 0}});
  // aparams = (st) ? AudioParams({st->codecpar->codec_type, st->channels, st->channel_layout}) : AudioParams({AV_SAMPLE_FMT_NONE, 0, 0});
private:
  BasicMediaParams bparams;
  AudioParams aparams;
};
}
