#pragma once

#include <algorithm>

extern "C" {
#include <libavformat/avformat.h> // for AVFormatContext
}

#include "../Common/ffmpegException.h"
#include "ffmpegFifoBuffer.h"

namespace ffmpeg
{
typedef std::deque<AVPacket> AVPacketVector;

class AVPacketBuffer : public FifoBuffer<AVPacket>
{
public:
  AVPacketBuffer(const uint32_t size = 2, const double timeout_s = 0.0) : FifoBuffer<AVPacket>(0, timeout_s)
  {
    resize(size);
  };
  ~AVPacketBuffer()
  {
    reset();
  }

protected:
  virtual void reset_internal()
  {
    // dereference queued packets
    for (auto it = buffer.begin(); it!=buffer.end(); it++)
      av_packet_unref(&*it);

    // then empty the buffer
    FifoBuffer<AVPacket>::reset_internal();
  }
};
}
