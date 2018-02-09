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
class OutputStream : public BaseStream
{
public:
  OutputStream(IAVFrameSource *buf = NULL);
  virtual ~OutputStream();

  virtual bool ready();

  virtual AVStream *open();
  virtual void close() {}

  IAVFrameSource *setgetBuffer(IAVFrameSource *other_buf);
  void swapBuffer(IAVFrameSource *&other_buf);
  void setBuffer(IAVFrameSource *new_buf);
  IAVFrameSource *getBuffer() const;
  IAVFrameSource *releaseBuffer();

  virtual int reset() { avcodec_flush_buffers(ctx); return 0; } // reset decoder states
  virtual int OutputStream::processFrame(AVPacket *packet);

protected:
  IAVFrameSource *src;
  AVDictionary *encoder_opts;
};

typedef std::vector<OutputStream *> OutputStreamPtrs;

class OutputVideoStream : public OutputStream, virtual IVideoHandler
{
public:
  OutputVideoStream(IAVFrameSource *buf = NULL);
  virtual ~OutputVideoStream();

  const VideoParams &getVideoParams() const { return vparams; }

  AVStream *open()
  {
    AVStream *s = OutputStream::open();
    if (st)
    {
      AVCodecParameters *par = st->codecpar;
      vparams = {(AVPixelFormat)par->codec_type, par->width, par->height, par->sample_aspect_ratio};
    }
    return s;
  }
  void close()
  {
    bparams.type = AVMEDIA_TYPE_VIDEO;
    vparams = {AV_PIX_FMT_NONE, 0, 0, {0, 0}};
  }

  AVPixelFormats getPixelFormats() const;
  AVPixelFormat getPixelFormat() const;

  AVPixelFormat choose_pixel_fmt(AVPixelFormat target) const;
  AVPixelFormats choose_pix_fmts() const;

  using OutputStream::getBasicMediaParams;

private:
  bool keep_pix_fmt;
  VideoParams vparams;
};

class OutputAudioStream : public OutputStream, virtual IAudioHandler
{
public:
  OutputAudioStream(IAVFrameSource *buf = NULL)
      : OutputStream(buf), aparams({AV_SAMPLE_FMT_NONE, 0, 0})
  {
    bparams.type = AVMEDIA_TYPE_AUDIO;
  }
  virtual ~OutputAudioStream() {}

  const AudioParams &getAudioParams() const { return aparams; }

  AVStream *open()
  {
    AVStream *s = OutputStream::open();
    if (st)
    {
      AVCodecParameters *par = st->codecpar;
      aparams = {(AVSampleFormat)par->codec_type, par->channels, par->channel_layout};
    }
    return s;
  }
  void close()
  {
    OutputStream::close();
    bparams.type = AVMEDIA_TYPE_AUDIO;
    aparams = {AV_SAMPLE_FMT_NONE, 0, 0};
  }

  using OutputStream::getBasicMediaParams;

private:
  AudioParams aparams;
};
}
