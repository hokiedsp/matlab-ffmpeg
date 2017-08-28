#pragma once

extern "C" {
#include <libavformat/avformat.h> // for AVFormatContext
}

#include <algorithm>

#include "ffmpegFifoBuffer.h"
#include "ffmpegException.h"

namespace ffmpeg
{
typedef std::vector<AVPacket *> AVPacketPtrVector;

class AVPacketPtrBuffer : public FifoBuffer<AVPacket *>
{
public:
  AVPacketPtrBuffer(const uint32_t size = 2, const double timeout_s = 0.0) : FifoBuffer<AVPacket *>(0, timeout_s)
  {
    resize(size);
  };
  ~AVPacketPtrBuffer()
  {
    resize(0);
  }

protected:
  virtual void reset_internal()
  {
    // dereference queued packets
    for (int64_t i = rpos + 1; i < uint32_t(wpos > rpos ? wpos + 1 : buffer.size()); i++)
      av_packet_unref(buffer[i]);
    if (wpos < rpos)
      for (int64_t i = 0; i <= wpos; i++)
        av_packet_unref(buffer[i]);

    // set wpos & rpos to -1
    FifoBuffer<AVPacket *>::reset_internal();
  }

  virtual void resize_internal(const uint32_t size_new)
  {

    // save old size
    size_t size_old = buffer.size();

    // dereference queued packets
    reset_internal();

    // if shrinks, deallocate packets before letting them go
    for (AVPacketPtrVector::iterator it = buffer.begin() + size_new; it < buffer.end(); it++)
      av_packet_free(&*it);

    // resize buffer
    FifoBuffer<AVPacket *>::resize_internal(size_new);

    // create new AVPacket objects for each new buffer slot
    for (AVPacketPtrVector::iterator it = buffer.begin() + size_old; it < buffer.end(); it++)
    {
      *it = av_packet_alloc();
      if (*it == NULL)
        ffmpegException("Could not allocate memory for an AVPacket object.");
    }
  }
};
}
