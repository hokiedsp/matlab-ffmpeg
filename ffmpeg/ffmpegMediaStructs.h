#pragma once

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
  int width, height;
  AVRational sample_aspect_ratio;
  AVPixelFormat format;
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
};

class IVideoHandler
{
public:
  virtual const VideoParams &getVideoParams() const = 0;
};

class IAudioHandler
{
public:
  virtual const AudioParams &getAudioParams() const = 0;
};
