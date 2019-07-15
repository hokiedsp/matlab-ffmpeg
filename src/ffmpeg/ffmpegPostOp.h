#pragma once

extern "C"
{
#include <libavutil/frame.h> // for AVFrame
}

#include "ffmpegAVFrameBufferInterfaces.h"

namespace ffmpeg
{

/**
 * \brief Interface for ffmpeg Reader post filters
 */
class PostOpInterface
{
  public:
  virtual ~PostOpInterface() {}
  
  /**
   * \brief Perform the filter on the next frame in the associated
   * IAVFrameSourceBuffer object
   */
  virtual bool filter(AVFrame *dst) = 0;
};

class PostOpPassThru : public PostOpInterface
{
  public:
  PostOpPassThru(IAVFrameSourceBuffer &src) : in(src) {}
  ~PostOpPassThru() {}

  bool filter(AVFrame *dst) override
  {
    bool eof;
    in.pop(dst, &eof);
    return eof;
  }

  private:
  IAVFrameSourceBuffer &in;
};
} // namespace ffmpeg
