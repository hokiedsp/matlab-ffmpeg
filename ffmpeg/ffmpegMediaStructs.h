#pragma once

#ifdef WIN32
#pragma warning(disable : 4250)
#endif

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
}

struct BasicMediaParams
{
  AVMediaType type; // AVMEDIA_TYPE_SUBTITLE for sub2video
  AVRational time_base;

  bool isValid() const
  {
    return type != AVMEDIA_TYPE_UNKNOWN && time_base.num != 0 && time_base.den != 0;
  }
};

struct VideoParams
{
  AVPixelFormat format;
  int width;
  int height;
  AVRational sample_aspect_ratio;
  // AVRational frame_rate;
};

struct AudioParams
{
  AVSampleFormat format;
  int channels;
  uint64_t channel_layout;
  int sample_rate;
};

/////////////////////

class IMediaHandler
{
public:
  virtual BasicMediaParams getBasicMediaParams() const = 0;

  virtual AVMediaType getMediaType() const = 0;
  virtual std::string getMediaTypeString() const = 0;
  virtual AVRational getTimeBase() const = 0;
  virtual void setTimeBase(const AVRational &tb) = 0;
};

class IVideoHandler : virtual public IMediaHandler
{
public:
  virtual VideoParams getVideoParams() const = 0;
  virtual void setVideoParams(const VideoParams &params) = 0;
  virtual void setVideoParams(const IVideoHandler &other) = 0;

  virtual AVPixelFormat getFormat() const = 0;
  virtual std::string getFormatName() const = 0;
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
  virtual AudioParams getAudioParams() const = 0;
  virtual void setAudioParams(const AudioParams &params) = 0;
  virtual void setAudioParams(const IAudioHandler &other) = 0;

  virtual AVSampleFormat getFormat() const = 0;
  virtual std::string getFormatName() const = 0;
  virtual int getChannels() const = 0;
  virtual uint64_t getChannelLayout() const = 0;
  virtual int getSampleRate() const = 0;

  virtual void setFormat(const AVSampleFormat fmt) = 0;
  virtual void getChannels(const int ch) = 0;
  virtual void setChannelLayout(const uint64_t layout) = 0;
  virtual void getSampleRate(const int fs) = 0;
};

/////////////////////

class MediaHandler : protected BasicMediaParams, virtual public IMediaHandler
{
public:
  MediaHandler(const AVMediaType t = AVMEDIA_TYPE_UNKNOWN, const AVRational &tb = {0, 0}) : BasicMediaParams({t, tb}) {}
  MediaHandler(const IMediaHandler &other) : BasicMediaParams(other.getBasicMediaParams()) {}

  BasicMediaParams getBasicMediaParams() const { return *this; }
  const BasicMediaParams &getBasicMediaParamsRef() const { return *this; }

  AVMediaType getMediaType() const { return type; }
  std::string getMediaTypeString() const { return av_get_media_type_string(type); }
  AVRational getTimeBase() const { return time_base; }
  const AVRational &getTimeBaseRef() const { return time_base; }
  void setTimeBase(const AVRational &tb) { time_base = tb; }

  virtual bool ready() const { return type != AVMEDIA_TYPE_UNKNOWN && time_base.num != 0 && time_base.den != 0; }
};

struct VideoHandler : protected VideoParams, virtual public IVideoHandler
{
  VideoHandler(const AVPixelFormat fmt = AV_PIX_FMT_NONE, const int w = 0, const int h = 0, const AVRational &sar = {0, 0})
      : VideoParams({fmt, w, h, sar}) {}
  VideoHandler(const IVideoHandler &other) : VideoParams(other.getVideoParams()) {}

  VideoParams getVideoParams() const { return *static_cast<const VideoParams *>(this); }
  const VideoParams &getVideoParamsRef() const { return *this; }
  void setVideoParams(const VideoParams &params) { *static_cast<VideoParams *>(this) = params; }
  void setVideoParams(const IVideoHandler &other) { *static_cast<VideoParams *>(this) = other.getVideoParams(); }

  AVPixelFormat getFormat() const { return format; }
  std::string getFormatName() const { return av_get_pix_fmt_name(getFormat()); }
  int getWidth() const { return width; };
  int getHeight() const { return height; };
  AVRational getSAR() const { return sample_aspect_ratio; }
  const AVRational &getSARRef() const { return sample_aspect_ratio; }

  void setFormat(const AVPixelFormat fmt) { format = fmt; }
  void setWidth(const int w) { width = w; };
  void setHeight(const int h) { height = h; };
  void setSAR(const AVRational &sar) { sample_aspect_ratio = sar; }

  virtual bool ready() const
  {
    return format != AV_PIX_FMT_NONE && width != 0 && height != 0 &&
           sample_aspect_ratio.den != 0 && sample_aspect_ratio.num != 0;
  }
};

struct AudioHandler : protected AudioParams, virtual public IAudioHandler
{
  AudioHandler(const AVSampleFormat fmt = AV_SAMPLE_FMT_NONE, const int ch = 0, const uint64_t layout = 0, const int fs = 0)
      : AudioParams({fmt, ch, layout, fs}) {}
  AudioHandler(const IAudioHandler &other) : AudioParams(other.getAudioParams()) {}

  AudioParams getAudioParams() const { return *static_cast<const AudioParams *>(this); }
  const AudioParams &getAudioParamsRef() const { return *this; }
  void setAudioParams(const AudioParams &params) { *static_cast<AudioParams *>(this) = params; }
  void setAudioParams(const IAudioHandler &other) { *static_cast<AudioParams *>(this) = other.getAudioParams(); }

  AVSampleFormat format;
  int channels;
  uint64_t channel_layout;
  int sample_rate;

  AVSampleFormat getFormat() const { return format; }
  std::string getFormatName() const { return av_get_sample_fmt_name(getFormat()); }
  int getChannels() const { return channels; };
  uint64_t getChannelLayout() const { return channel_layout; };
  int getSampleRate() const { return sample_rate; }

  void setFormat(const AVSampleFormat fmt) { format = fmt; }
  void getChannels(const int ch) { channels = ch; };
  void setChannelLayout(const uint64_t layout) { channel_layout = layout; };
  void getSampleRate(const int fs) { sample_rate = fs; }

  virtual bool ready() const
  {
    return format != AV_SAMPLE_FMT_NONE && channels != 0 && channel_layout != 0 && sample_rate != 0;
  }
};
