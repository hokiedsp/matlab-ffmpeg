#pragma once

extern "C" {
#include <libavcodec/avcodec.h> // for AVFormatContext
}

#include <algorithm>

#include "ffmpegFiFoBuffer.h"
#include "../Common/ffmpegException.h"

namespace ffmpeg
{
struct AVFramePtrContainer : public FifoContainer<AVFrame *>
{
  AVFramePtrContainer()
  {
    data = av_frame_alloc();
    if (!data)
      throw ffmpegException("Failed to allocate memory for AVFrame.");
  }
  ~AVFramePtrContainer()
  {
    av_frame_free(&data);
  }

  virtual void init()
  {
    FifoContainer<AVFrame *>::init();
    av_frame_unref(data);
  }

  virtual AVFrame **write_init()
  {
    AVFrame **rval = FifoContainer<AVFrame *>::write_init();
    av_frame_unref(data);
    return rval;
  }
};

typedef FifoBuffer<AVFrame *, AVFramePtrContainer> AVFramePtrBuffer;
}
