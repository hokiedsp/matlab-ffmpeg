#pragma once

extern "C" {
#include <libavcodec/avcodec.h> // for AVFormatContext
}

#include "ffmpegFifoBuffer.h"

#include <mex.h>

namespace ffmpeg
{

struct AVPacketContainer : public FifoContainer<AVPacket>
{
  AVPacketContainer()
  {
    av_init_packet(&data);
    data.data = NULL;
    data.size = 0;
  }
  ~AVPacketContainer()
  {
    av_packet_unref(&data);
  }

  virtual void init()
  {
    FifoContainer<AVPacket>::init();
    av_packet_unref(&data);
  }

  virtual AVPacket *write_init()
  {
    AVPacket *rval = FifoContainer<AVPacket>::write_init();
    av_packet_unref(&data);
    return rval;
  }
};

typedef FifoBuffer<AVPacket, AVPacketContainer> AVPacketBuffer;
}
