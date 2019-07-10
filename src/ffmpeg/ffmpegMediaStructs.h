#pragma once

#ifdef WIN32
#pragma warning(disable : 4250)
#endif

#include "ffmpegException.h"

extern "C"
{
#include <libavutil/avutil.h>    // AVMediaType
#include <libavutil/pixfmt.h>    // AVPixelFormat
#include <libavutil/rational.h>  //AVRational
#include <libavutil/samplefmt.h> // AVSampleFormat
}

namespace ffmpeg
{
struct MediaParams
{
  AVMediaType type; // AVMEDIA_TYPE_SUBTITLE for sub2video
  AVRational time_base;

  MediaParams(const AVMediaType t, const AVRational &tb)
      : type(t), time_base(tb){};
  virtual ~MediaParams(){};
};

struct VideoParams : public MediaParams
{
  AVPixelFormat format;
  int width;
  int height;
  AVRational sample_aspect_ratio;
  AVRational frame_rate;

  VideoParams(const AVRational &tb = {0, 0},
              const AVPixelFormat fmt = AV_PIX_FMT_NONE, const int w = 0,
              const int h = 0, const AVRational &sar = {0, 0},
              const AVRational &fs = {0, 0})
      : MediaParams(AVMEDIA_TYPE_VIDEO, tb), format(fmt), width(w), height(h),
        sample_aspect_ratio(sar), frame_rate(fs){};
};

struct AudioParams : public MediaParams
{
  AVSampleFormat format;
  uint64_t channel_layout;
  int sample_rate;

  AudioParams(const AVRational &tb = {0, 0},
              const AVSampleFormat fmt = AV_SAMPLE_FMT_NONE,
              const uint64_t layout = 0, const int fs = 0)
      : MediaParams(AVMEDIA_TYPE_AUDIO, tb), format(fmt),
        channel_layout(layout), sample_rate(fs){};
};
} // namespace ffmpeg
