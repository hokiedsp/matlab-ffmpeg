#pragma once

#include "ffmpegBase.h"
// #include "ffmpegAvRedefine.h"
#include "ffmpegAVFrameBufferInterfaces.h"

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
class BaseStream : public Base
{
public:
  BaseStream();
  virtual ~BaseStream();

  virtual bool ready();

  virtual void open(AVStream *st)=0;
  virtual void close();

  virtual int reset(); // reset decoder states
  
  AVStream *getAVStream() const;
  int getId() const;
  AVMediaType getAVMediaType() const;

  const AVCodec *getAVCodec() const;
  std::string getCodecName() const;
  std::string getCodecDescription() const;
  bool getCodecFlags(const int mask = ~0) const;
  AVRational getTimeBase() const;
  int64_t getLastFrameTimeStamp() const;

  static const AVPixelFormats get_compliance_unofficial_pix_fmts(AVCodecID codec_id, const AVPixelFormats default_formats);
  void choose_sample_fmt();// should be moved to OutputAudioStream when created

protected:
  AVStream *st;        // stream
  AVCodecContext *ctx; // stream's codec context
  int64_t pts; // pts of the last frame 
};

}
