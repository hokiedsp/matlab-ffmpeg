#pragma once

#ifdef WIN32
#pragma warning(disable : 4250)
#endif

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
}

struct BasicMediaParams
{
  AVMediaType type; // AVMEDIA_TYPE_SUBTITLE for sub2video
  AVRational time_base;
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
  // int sample_rate;
};

class IMediaHandler
{
public:
  virtual const BasicMediaParams &getBasicMediaParams() const = 0;
  virtual AVMediaType getMediaType() const = 0;
  virtual AVRational getTimeBase() const = 0;
};

class IVideoHandler : virtual public IMediaHandler
{
public:
  virtual const VideoParams &getVideoParams() const = 0;
};

class IAudioHandler : virtual public IMediaHandler
{
public:
  virtual const AudioParams &getAudioParams() const = 0;
};
