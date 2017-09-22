#pragma once

extern "C" {
#include <libavcodec/avcodec.h> // for AVFormatContext
}

#include <algorithm>

#include "ffmpegFiFoBuffer.h"
#include "../Common/ffmpegException.h"

namespace ffmpeg
{
typedef std::vector<AVFrame> AVFramePtrVector;

class AVFramePtrBuffer : public FifoBuffer<AVFrame *>
{
public:
  AVFramePtrBuffer(const uint32_t size = 2, const double timeout_s = 0.0) : FifoBuffer<AVFrame *>(0, timeout_s)
  {
    resize(size);
  }
  ~AVFramePtrBuffer()
  {
    reset();
  }

protected:
  virtual void reset_internal()
  {
    // dereference queued packets
    for (auto it = buffer.begin(); it != buffer.end(); it++)
      av_frame_unref(*it);

    // then empty the buffer
    FifoBuffer<AVFrame*>::reset_internal();
  }
};
}
