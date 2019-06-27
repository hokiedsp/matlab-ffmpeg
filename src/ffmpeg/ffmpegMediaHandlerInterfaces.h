#pragma once

#include "ffmpegMediaStructs.h"

extern "C"
{
  // #include <libavutil/avutil.h>
  // #include <libavutil/channel_layout.h>
  // #include <libavutil/frame.h>
  #include <libavutil/pixdesc.h>
  // #include <libavutil/rational.h>
  // #include <libavutil/samplefmt.h>
}

namespace ffmpeg
{

class IMediaHandler
{
  public:
  virtual const MediaParams &getMediaParams() const = 0;
  virtual void setMediaParams(const MediaParams &new_params) = 0;
  virtual void setMediaParams(const IMediaHandler &other) = 0;

  virtual AVMediaType getMediaType() const = 0;
  virtual std::string getMediaTypeString() const = 0;
  virtual AVRational getTimeBase() const = 0;

  virtual void setTimeBase(const AVRational &tb) = 0;

  virtual bool ready() const = 0;
};

class IVideoHandler : virtual public IMediaHandler
{
  public:
  virtual AVPixelFormat getFormat() const = 0;
  virtual std::string getFormatName() const = 0;
  virtual const AVPixFmtDescriptor &getFormatDescriptor() const = 0;
  virtual int getWidth() const = 0;
  virtual int getHeight() const = 0;
  virtual AVRational getSAR() const = 0;

  virtual void setFormat(const AVPixelFormat fmt) = 0;
  virtual void setWidth(const int w) = 0;
  virtual void setHeight(const int h) = 0;
  virtual void setSAR(const AVRational &sar) = 0;
};

class IAudioHandler : virtual public IMediaHandler
{
  public:
  virtual AVSampleFormat getFormat() const = 0;
  virtual std::string getFormatName() const = 0;
  virtual int getChannels() const = 0;
  virtual uint64_t getChannelLayout() const = 0;
  virtual std::string getChannelLayoutName() const = 0;
  virtual int getSampleRate() const = 0;

  virtual void setFormat(const AVSampleFormat fmt) = 0;
  virtual void setChannelLayout(const uint64_t layout) = 0;
  virtual void setChannelLayoutByName(const std::string &name) = 0;
  virtual void setSampleRate(const int fs) = 0;
};

} // namespace ffmpeg
