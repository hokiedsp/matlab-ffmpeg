#pragma once

#include "ffmpegBase.h"
// #include "ffmpegAvRedefine.h"
#include "ffmpegAVFrameBufferInterfaces.h"
#include "ffmpegMediaStructs.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
// #include <libavutil/pixdesc.h>
}

typedef std::vector<AVPixelFormat> AVPixelFormats;

namespace ffmpeg
{
/**
 * \brief Class to manage AVStream
 */
class BaseStream : public Base, virtual public IMediaHandler
{
public:
  BaseStream();
  virtual ~BaseStream();

  BasicMediaParams getBasicMediaParams() const;
  AVMediaType getMediaType() const;
  std::string getMediaTypeString() const;
  AVRational getTimeBase() const;
  void setTimeBase(const AVRational &tb);

  virtual bool ready();

  virtual void close();

  virtual int reset(); // reset decoder states

  AVStream *getAVStream() const;
  int getId() const;

  const AVCodec *getAVCodec() const;
  std::string getCodecName() const;
  std::string getCodecDescription() const;
  bool getCodecFlags(const int mask = ~0) const;

  int getCodecFrameSize() const;

  int64_t getLastFrameTimeStamp() const;

  static const AVPixelFormats get_compliance_unofficial_pix_fmts(AVCodecID codec_id, const AVPixelFormats default_formats);
  void choose_sample_fmt(); // should be moved to OutputAudioStream when created

protected:
  AVStream *st;        // stream
  AVCodecContext *ctx; // stream's codec context
  int64_t pts;         // pts of the last frame
};

/**
 * Implements all the functions for IVideoHandler interface
 */
class VideoStream : virtual public BaseStream, virtual public IVideoHandler
{
  // IVideoHandler interface functions
  VideoParams getVideoParams() const
  {
    return ctx ? VideoParams({ctx->pix_fmt, ctx->width, ctx->height, ctx->sample_aspect_ratio})
               : VideoParams({AV_PIX_FMT_NONE, 0, 0, AVRational({0, 0})});
  }
  AVPixelFormat getFormat() const { return ctx ? ctx->pix_fmt : AV_PIX_FMT_NONE; }
  std::string getFormatName() const { return ctx ? av_get_pix_fmt_name(ctx->pix_fmt) : ""; }
  int getWidth() const { return ctx ? ctx->width : 0; }
  int getHeight() const { return ctx ? ctx->height : 0; }
  AVRational getSAR() const { return ctx ? ctx->sample_aspect_ratio : AVRational({0, 0}); }

  void setVideoParams(const VideoParams &params);
  void setVideoParams(const IVideoHandler &other);
  void setValidVideoParams(const VideoParams &params);
  void setValidVideoParams(const IVideoHandler &other);
  void setFormat(const AVPixelFormat fmt);
  void setWidth(const int w);
  void setHeight(const int h);
  void setSAR(const AVRational &sar);
};

class AudioStream : virtual public BaseStream, virtual public IAudioHandler
{
  AudioParams getAudioParams() const
  {
    return ctx ? AudioParams({ctx->sample_fmt, ctx->channel_layout, ctx->sample_rate})
               : AudioParams({AV_SAMPLE_FMT_NONE, 0, 0});
  }

  AVSampleFormat getFormat() const { return ctx ? ctx->sample_fmt : AV_SAMPLE_FMT_NONE; }
  std::string getFormatName() const { return ctx ? av_get_sample_fmt_name(ctx->sample_fmt) : ""; }
  int getChannels() const { return ctx ? av_get_channel_layout_nb_channels(ctx->channel_layout) : 0; };
  uint64_t getChannelLayout() const { return ctx ? ctx->channel_layout : 0; };
  std::string getChannelLayoutName() const;
  int getSampleRate() const { return ctx ? ctx->sample_rate : 0; }

  void setAudioParams(const AudioParams &params);
  void setAudioParams(const IAudioHandler &other);
  void setValidAudioParams(const AudioParams &params);
  void setValidAudioParams(const IAudioHandler &other);
  void setFormat(const AVSampleFormat fmt);
  void setChannelLayout(const uint64_t layout);
  void setChannelLayoutByName(const std::string &name);
  void setSampleRate(const int fs);
};
}
