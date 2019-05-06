#pragma once

#ifdef WIN32
#pragma warning(disable : 4250)
#endif

#include "ffmpegException.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}

namespace ffmpeg
{
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
  virtual void setValidVideoParams(const VideoParams &params) = 0;
  virtual void setValidVideoParams(const IVideoHandler &other) = 0;

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
  virtual void setValidAudioParams(const AudioParams &params) = 0;
  virtual void setValidAudioParams(const IAudioHandler &other) = 0;

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

/////////////////////

class MediaHandler : protected BasicMediaParams, virtual public IMediaHandler
{
protected:
  MediaHandler() : BasicMediaParams({AVMEDIA_TYPE_UNKNOWN, AVRational({0, 0})})
  {
    av_log(NULL, AV_LOG_INFO, "[MediaHandler:default] default constructor\n");
  }

public:
  MediaHandler(const AVMediaType t, const AVRational &tb = {0, 0}) : BasicMediaParams({t, tb})
  {
    av_log(NULL, AV_LOG_INFO, "[MediaHandler:regular] time_base:%d/%d\n", tb.num, tb.den);
    av_log(NULL, AV_LOG_INFO, "[MediaHandler:regular] mediatype:%s\n", av_get_media_type_string(t));
  }
  MediaHandler(const IMediaHandler &other) : BasicMediaParams(other.getBasicMediaParams()) {}

  BasicMediaParams getBasicMediaParams() const { return *this; }
  const BasicMediaParams &getBasicMediaParamsRef() const { return *this; }

  AVMediaType getMediaType() const { return type; }
  std::string getMediaTypeString() const { return (type == AVMEDIA_TYPE_UNKNOWN) ? "unknown" : av_get_media_type_string(type); }
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
  void setValidVideoParams(const VideoParams &params)
  {
    if (params.format != AV_PIX_FMT_NONE)
      format = params.format;
    if (params.width > 0)
      width = params.width;
    if (params.height > 0)
      height = params.height;
    if (params.sample_aspect_ratio.num > 0 && params.sample_aspect_ratio.den > 0)
      sample_aspect_ratio = params.sample_aspect_ratio;
  }
  void setValidVideoParams(const IVideoHandler &other) { setValidVideoParams(other.getVideoParams()); }

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
  AudioHandler(const AVSampleFormat fmt = AV_SAMPLE_FMT_NONE, const uint64_t layout = 0, const int fs = 0)
      : AudioParams({fmt, layout, fs}) {}
  AudioHandler(const IAudioHandler &other) : AudioParams(other.getAudioParams()) {}

  AudioParams getAudioParams() const { return *static_cast<const AudioParams *>(this); }
  const AudioParams &getAudioParamsRef() const { return *this; }
  void setAudioParams(const AudioParams &params) { *static_cast<AudioParams *>(this) = params; }
  void setAudioParams(const IAudioHandler &other) { *static_cast<AudioParams *>(this) = other.getAudioParams(); }
  void setValidAudioParams(const AudioParams &params)
  {
    if (params.format != AV_SAMPLE_FMT_NONE)
      format = params.format;
    if (params.sample_rate > 0)
      sample_rate = params.sample_rate;
    if (params.channel_layout)
      channel_layout = params.channel_layout;
  }
  void setValidAudioParams(const IAudioHandler &other) { setValidAudioParams(other.getAudioParams()); }

  AVSampleFormat getFormat() const { return format; }
  std::string getFormatName() const { return av_get_sample_fmt_name(getFormat()); }
  int getChannels() const { return av_get_channel_layout_nb_channels(channel_layout); };
  uint64_t getChannelLayout() const { return channel_layout; };
  std::string getChannelLayoutName() const
  {
    int nb_channels = av_get_channel_layout_nb_channels(channel_layout);
    if (nb_channels)
    {
      char buf[1024];
      av_get_channel_layout_string(buf, 1024, nb_channels, channel_layout);
      return buf;
    }
    else
      return "";
  }
  int getSampleRate() const { return sample_rate; }

  void setFormat(const AVSampleFormat fmt) { format = fmt; }
  void setChannelLayout(const uint64_t layout) { channel_layout = layout; };
  void setChannelLayoutByName(const std::string &name) { channel_layout = av_get_channel_layout(name.c_str()); }
  void setSampleRate(const int fs) { sample_rate = fs; }

  virtual bool ready() const
  {
    return format != AV_SAMPLE_FMT_NONE && !channel_layout && sample_rate > 0;
  }
};

///////////////////////////////////////////////////////////////////////////////////////////////

/**
 * \brief Proxy Media Handler
 */
class MediaHandlerProxy : virtual public IMediaHandler
{
protected:
  MediaHandlerProxy() : src(NULL) {}
  MediaHandlerProxy(IMediaHandler &base) : src(&base) {}

public:
  BasicMediaParams getBasicMediaParams() const { return src ? src->getBasicMediaParams() : BasicMediaParams({AVMEDIA_TYPE_UNKNOWN, {0, 0}}); }

  AVMediaType getMediaType() const { return src ? src->getMediaType() : AVMEDIA_TYPE_UNKNOWN; }
  std::string getMediaTypeString() const { return src ? src->getMediaTypeString() : "unknown"; }
  AVRational getTimeBase() const { return src ? src->getTimeBase() : AVRational({0, 0}); }
  void setTimeBase(const AVRational &tb)
  {
    if (src)
      src->setTimeBase(tb);
    else
      throw ffmpegException("[ffmpeg::MediaHandlerProxy::setTimeBase] Proxy not connected.");
  }

protected:
  IMediaHandler *src;

  void attach_proxy(IMediaHandler &base) { src = &base; }
  void detach_proxy() { src = NULL; }
};

struct VideoHandlerProxy : virtual public IVideoHandler
{
  VideoHandlerProxy() : src(NULL) {}
  VideoHandlerProxy(IVideoHandler &base) : src(&base) {}

  VideoParams getVideoParams() const { return src ? src->getVideoParams() : VideoParams({AV_PIX_FMT_NONE, 0, 0, {0, 0}}); }
  AVPixelFormat getFormat() const { return src ? src->getFormat() : AV_PIX_FMT_NONE; }
  std::string getFormatName() const { return src ? src->getFormatName() : ""; }
  int getWidth() const { return src ? src->getWidth() : 0; };
  int getHeight() const { return src ? src->getHeight() : 0; };
  AVRational getSAR() const { return src ? src->getSAR() : AVRational({0, 0}); }

  void setValidVideoParams(const VideoParams &params)
  {
    if (src)
      src->setValidVideoParams(params);
    else
      throw ffmpegException("[ffmpeg::VideoHandlerProxy::setValidVideoParams] Proxy not connected.");
  }
  void setValidVideoParams(const IVideoHandler &other)
  {
    if (src)
      src->setValidVideoParams(other);
    else
      throw ffmpegException("[ffmpeg::VideoHandlerProxy::setValidVideoParams] Proxy not connected.");
  }
  void setVideoParams(const VideoParams &params)
  {
    if (src)
      src->setVideoParams(params);
    else
      throw ffmpegException("[ffmpeg::VideoHandlerProxy::setVideoParams] Proxy not connected.");
  }
  void setVideoParams(const IVideoHandler &other)
  {
    if (src)
      src->setVideoParams(other.getVideoParams());
    else
      throw ffmpegException("[ffmpeg::VideoHandlerProxy::setVideoParams] Proxy not connected.");
  }
  void setFormat(const AVPixelFormat fmt)
  {
    if (src)
      src->setFormat(fmt);
    else
      throw ffmpegException("[ffmpeg::VideoHandlerProxy::setFormat] Proxy not connected.");
  }

  void setWidth(const int w)
  {
    if (src)
      src->setWidth(w);
    else
      throw ffmpegException("[ffmpeg::VideoHandlerProxy::setWidth] Proxy not connected.");
  }

  void setHeight(const int h)
  {
    if (src)
      src->setHeight(h);
    else
      throw ffmpegException("[ffmpeg::VideoHandlerProxy::setHeight] Proxy not connected.");
  }

  void setSAR(const AVRational &sar)
  {
    if (src)
      src->setSAR(sar);
    else
      throw ffmpegException("[ffmpeg::VideoHandlerProxy::setSAR] Proxy not connected.");
  }

protected:
  IVideoHandler *src;

  void attach_proxy(IVideoHandler &base) { src = &base; }
  void detach_proxy() { src = NULL; }
};

struct AudioHandlerProxy : virtual public IAudioHandler
{
  AudioHandlerProxy() : src(NULL) {}
  AudioHandlerProxy(IAudioHandler &base) : src(&base) {}

  AudioParams getAudioParams() const { return src ? src->getAudioParams() : AudioParams({AV_SAMPLE_FMT_NONE, 0, 0}); }
  AVSampleFormat getFormat() const { return src ? src->getFormat() : AV_SAMPLE_FMT_NONE; }
  std::string getFormatName() const { return src ? src->getFormatName() : ""; }
  int getChannels() const { return src ? src->getChannels() : 0; };
  uint64_t getChannelLayout() const { return src ? src->getChannelLayout() : 0; };
  virtual std::string getChannelLayoutName() { return src ? src->getChannelLayoutName() : ""; };
  int getSampleRate() const { return src ? src->getSampleRate() : 0; }

  void setAudioParams(const AudioParams &params)
  {
    if (src)
      src->setAudioParams(params);
    else
      throw ffmpegException("[ffmpeg::AudioHandlerProxy::setAudioParams] Proxy not connected.");
  }
  void setAudioParams(const IAudioHandler &other)
  {
    if (src)
      src->setAudioParams(other);
    else
      throw ffmpegException("[ffmpeg::AudioHandlerProxy::setAudioParams] Proxy not connected.");
  }
  void setValidAudioParams(const AudioParams &params)
  {
    if (src)
      src->setValidAudioParams(params);
    else
      throw ffmpegException("[ffmpeg::AudioHandlerProxy::setValidAudioParams] Proxy not connected.");
  }
  void setValidAudioParams(const IAudioHandler &other)
  {
    if (src)
      src->setValidAudioParams(other);
    else
      throw ffmpegException("[ffmpeg::AudioHandlerProxy::setValidAudioParams] Proxy not connected.");
  }
  void setFormat(const AVSampleFormat fmt)
  {
    if (src)
      src->setFormat(fmt);
    else
      throw ffmpegException("[ffmpeg::AudioHandlerProxy::setFormat] Proxy not connected.");
  }
  void setChannelLayout(const uint64_t layout)
  {
    if (src)
      src->setChannelLayout(layout);
    else
      throw ffmpegException("[ffmpeg::AudioHandlerProxy::setChannelLayout] Proxy not connected.");
  };
  void setChannelLayoutByName(const std::string &name)
  {
    if (src)
      src->setChannelLayoutByName(name);
    else
      throw ffmpegException("[ffmpeg::AudioHandlerProxy::setChannelLayoutByName] Proxy not connected.");
  };
  void setSampleRate(const int fs)
  {
    if (src)
      src->setSampleRate(fs);
    else
      throw ffmpegException("[ffmpeg::AudioHandlerProxy::setSampleRate] Proxy not connected.");
  }

protected:
  IAudioHandler *src;

  void attach_proxy(IAudioHandler &base) { src = &base; }
  void detach_proxy() { src = NULL; }
};
////////////////////////////////

/**
 * \brief Base class of VideoAVFrameHandler & AudioAVFrameHandler
 * 
 * AVFrameHandler wraps an AVFrame object, managing one instance through its life
 * span. The derived classes may access \ref frame to perform all AVFrame related
 * operations but shall not call av_frame_free() on \ref frame at any point.
 * 
 */
struct AVFrameHandler
{
public:
  virtual ~AVFrameHandler()
  {
    av_frame_free(&frame);
  }

protected:
  AVFrameHandler()
  {
    frame = av_frame_alloc();
    if (!frame)
      throw ffmpegException("[ffmpeg::filter::AVFrameImageComponentSource]Failed to allocate AVFrame.");
  }
  // copy constructor
  AVFrameHandler(const AVFrameHandler &other)
  {
    frame = av_frame_clone(other.frame);
    if (!frame)
      throw ffmpegException("[ffmpeg::filter::AVFrameImageComponentSource]Failed to clone AVFrame.");
  }

  // move constructor
  AVFrameHandler(AVFrameHandler &&other) noexcept
      : frame(other.frame)
  {
    other.frame = av_frame_alloc();
  }

  /**
   * \brief Reallocate object's AVFrame 
   * 
   * release_frame() unreferences the existing frame but maintains
   * the parameter values.
   * 
   */
  virtual void release_frame()
  {
    // release the FFmpeg frame buffers
    av_frame_unref(frame);
  }

  AVFrame *frame;
};

struct VideoAVFrameHandler : public AVFrameHandler, virtual public IVideoHandler
{
  VideoAVFrameHandler() {}
  VideoAVFrameHandler(IVideoHandler &base) { setVideoParams(base); }

  bool validVideoParams() const
  {
    return ((AVPixelFormat)frame->format != AV_PIX_FMT_NONE) &&
           frame->width > 0 && frame->height > 0 &&
           frame->sample_aspect_ratio.num != 0 && frame->sample_aspect_ratio.den != 0;
  }

  // implement IVideoHandler functions
  VideoParams getVideoParams() const
  {
    return VideoParams({(AVPixelFormat)frame->format, frame->width, frame->height, frame->sample_aspect_ratio});
  }
  AVPixelFormat getFormat() const { return (AVPixelFormat)frame->format; }
  std::string getFormatName() const { return ((AVPixelFormat)frame->format != AV_PIX_FMT_NONE) ? av_get_pix_fmt_name((AVPixelFormat)frame->format) : ""; }
  int getWidth() const { return frame->width; }
  int getHeight() const { return frame->height; }
  AVRational getSAR() const { return frame->sample_aspect_ratio; }

  void setVideoParams(const VideoParams &params)
  {
    bool critical_change = frame->format != (int)params.format && frame->width != params.width && frame->height != params.height;

    // if no parameters have changed, exit
    if (!(critical_change || av_cmp_q(frame->sample_aspect_ratio, params.sample_aspect_ratio)))
      return;

    // if data critical parameters have changed, free frame data
    if (critical_change)
      release_frame();

    // copy new parameter values
    frame->format = (int)params.format;
    frame->width = params.width;
    frame->height = params.height;
    frame->sample_aspect_ratio = params.sample_aspect_ratio;
  }
  void setVideoParams(const IVideoHandler &other) { setVideoParams(other.getVideoParams()); }

  void setValidVideoParams(const VideoParams &params)
  {
    bool critical_change = params.format != AV_PIX_FMT_NONE && frame->format != (int)params.format &&
                           params.width > 0 && frame->width != params.width &&
                           params.height > 0 && frame->height != params.height;

    // if no parameters have changed, exit
    if (!(critical_change &&
          params.sample_aspect_ratio.num > 0 && params.sample_aspect_ratio.den > 0 &&
          av_cmp_q(frame->sample_aspect_ratio, params.sample_aspect_ratio)))
      return;

    // if data critical parameters have changed, free frame data
    if (critical_change)
      release_frame();

    // copy new parameter values
    if (params.format != AV_PIX_FMT_NONE)
      frame->format = (int)params.format;
    if (params.width > 0)
      frame->width = params.width;
    if (params.height > 0)
      frame->height = params.height;
    if (params.sample_aspect_ratio.num > 0 && params.sample_aspect_ratio.den > 0)
      frame->sample_aspect_ratio = params.sample_aspect_ratio;
  }
  void setValidVideoParams(const IVideoHandler &other) { setValidVideoParams(other.getVideoParams()); }

  void setFormat(const AVPixelFormat fmt)
  {
    if (frame->format == (int)fmt)
      return;
    release_frame();
    frame->format = (int)fmt;
  }
  void setWidth(const int w)
  {
    if (frame->width == w)
      return;
    release_frame();
    frame->width = w;
  }
  void setHeight(const int h)
  {
    if (frame->height == h)
      return;
    release_frame();
    frame->height = h;
  }
  void setSAR(const AVRational &sar)
  {
    if (!av_cmp_q(frame->sample_aspect_ratio, sar))
      return;
    frame->sample_aspect_ratio = sar;
  }

protected:
  /**
   * \brief Reallocate object's AVFrame 
   * 
   * release_frame() unreferences the existing frame but maintains
   * the parameter values.
   * 
   */
  virtual void release_frame()
  {
    // if no data buffers allocated, nothing to release
    // if (!frame->buf[0]) return;

    // save the image parameters
    VideoParams params = getVideoParams();

    // release the FFmpeg frame buffers
    av_frame_unref(frame);

    // copy back image parameters, cannot use setVideoParams() because it fires release_frame() recursively
    setVideoParams(params);
  }
};

struct AudioAVFrameHandler : public AVFrameHandler, virtual public IAudioHandler
{
  AudioAVFrameHandler() {}
  AudioAVFrameHandler(IAudioHandler &base) { setAudioParams(base); }

  bool validAudioParams() const
  {
    return ((AVSampleFormat)frame->format != AV_SAMPLE_FMT_NONE) &&
           getChannels() > 0 && frame->sample_rate > 0;
  }

  // implement IAudioHandler functions
  AudioParams getAudioParams() const
  {
    return AudioParams({(AVSampleFormat)frame->format, frame->channel_layout, frame->sample_rate});
  }
  AVSampleFormat getFormat() const { return (AVSampleFormat)frame->format; }
  std::string getFormatName() const { return av_get_sample_fmt_name((AVSampleFormat)frame->format); }
  int getChannels() const { return av_get_channel_layout_nb_channels(frame->channel_layout); }
  uint64_t getChannelLayout() const { return frame->channel_layout; }
  std::string getChannelLayoutName() const
  {
    int nb_channels = av_get_channel_layout_nb_channels(frame->channel_layout);
    if (nb_channels)
    {
      char buf[1024];
      av_get_channel_layout_string(buf, 1024, nb_channels, frame->channel_layout);
      return buf;
    }
    else
      return "";
  }
  int getSampleRate() const { return frame->sample_rate; }

  void setAudioParams(const AudioParams &params)
  {
    bool critical_change = frame->format != (int)params.format && frame->channel_layout != params.channel_layout &&
                           frame->sample_rate != params.sample_rate;

    // if no parameters have changed, exit
    if (!critical_change)
      return;

    // if data critical parameters have changed, free frame data
    if (critical_change)
      release_frame();

    // copy new parameter values
    frame->format = (int)params.format;
    frame->channel_layout = params.channel_layout;
    frame->sample_rate = params.sample_rate;
  }
  void setAudioParams(const IAudioHandler &other) { setAudioParams(other.getAudioParams()); }

  void setValidAudioParams(const AudioParams &params)
  {
    bool critical_change = params.format != AV_SAMPLE_FMT_NONE && frame->format != (int)params.format &&
                           params.channel_layout && frame->channel_layout != params.channel_layout &&
                           params.sample_rate > 0 && frame->sample_rate != params.sample_rate;

    // if no parameters have changed, exit
    if (!critical_change)
      return;

    // if data critical parameters have changed, free frame data
    if (critical_change)
      release_frame();

    // copy new parameter values
    if (params.format != AV_SAMPLE_FMT_NONE)
      frame->format = (int)params.format;
    if (params.channel_layout)
      frame->channel_layout = params.channel_layout;
    if (params.sample_rate > 0)
      frame->sample_rate = params.sample_rate;
  }
  void setValidAudioParams(const IAudioHandler &other) { setValidAudioParams(other.getAudioParams()); }

  void setFormat(const AVSampleFormat fmt)
  {
    if (frame->format == (int)fmt)
      return;
    release_frame();
    frame->format = (int)fmt;
  }
  void setChannelLayout(const uint64_t layout)
  {
    if (frame->channel_layout == layout)
      return;
    release_frame();
    frame->channel_layout = layout;
  }
  void setChannelLayoutByName(const std::string &name)
  {
    uint64_t layout = av_get_channel_layout(name.c_str());
    if (frame->channel_layout == layout)
      return;
    release_frame();
    frame->channel_layout = layout;
  }
  void setSampleRate(const int fs)
  {
    if (frame->sample_rate == fs)
      return;
    release_frame();
    frame->sample_rate = fs;
  }

protected:
  /**
   * \brief Reallocate object's AVFrame 
   * 
   * release_frame() unreferences the existing frame but maintains
   * the parameter values.
   * 
   */
  virtual void release_frame()
  {
    // save the image parameters
    AudioParams params = getAudioParams();

    // release the FFmpeg frame buffers
    av_frame_unref(frame);

    // copy back image parameters, cannot use setVideoParams() because it fires release_frame() recursively
    setAudioParams(params);
  }
};
}
