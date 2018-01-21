#pragma once

extern "C" {
#include <libavcodec/avcodec.h> // for AVFormatContext
}

#include <algorithm>

#include "ffmpegFiFoBuffer.h"
#include "ffmpegException.h"

namespace ffmpeg
{
struct AVFramePtrContainer : public FifoContainer<AVFrame *>
{
  AVFramePtrContainer() : unref_before_use(true)
  {
    data = av_frame_alloc();
    if (!data)
      throw ffmpegException("Failed to allocate memory for AVFrame.");
  }
  virtual ~AVFramePtrContainer()
  {
    av_frame_free(&data);
  }

  virtual void init()
  {
    FifoContainer<AVFrame *>::init();
    if (unref_before_use)
      av_frame_unref(data);
  }

  virtual AVFrame **write_init()
  {
    AVFrame **rval = FifoContainer<AVFrame *>::write_init();
    if (unref_before_use)
      av_frame_unref(data);
    return rval;
  }

  bool unref_before_use;
};

class AVFramePtrBuffer : public FifoBuffer<AVFrame *, AVFramePtrContainer>
{
public:
  AVFramePtrBuffer(const unsigned nelem = 0, const double timeout_s = 0.0, std::function<bool()> Predicate = std::function<bool()>()) : FifoBuffer(nelem, timeout_s, Predicate) {}
  virtual ~AVFramePtrBuffer() {}

  // change the frame configuration
  void lockPictureFrame(const int W, const int H, const AVPixelFormat FMT)
  {
    std::unique_lock<std::mutex> guard(lock);

    // flush the buffer
    flush_locked(true);

    // set frame content as picture and lock in the buffer
    for (FifoVector_t::iterator it = buffer.begin(); it < buffer.end(); it++)
    {
      //
      it->unref_before_use = false;

      // for convenience
      AVFrame *picture = it->data;

      // release the currently stored frame
      av_frame_unref(picture);

      // set all frames to as specified
      picture->width = W;
      picture->height = H;
      picture->format = FMT;
      av_frame_get_buffer(picture, 32);
    }
  }

  // change the frame configuration
  void unlockFrameBuffer()
  {
    std::unique_lock<std::mutex> guard(lock);

    // flush the buffer
    flush_locked(true);

    // unlock the internal buffer
    for (FifoVector_t::iterator it = buffer.begin(); it < buffer.end(); it++)
    {
      // release the currently stored frame
      av_frame_unref(it->data);

      // set flag to unref before each write
      it->unref_before_use = true;
    }
  }

protected:
  virtual void resize_locked(const uint32_t size)
  {
    // store the old size
    size_t old_size = buffer.size();

    // perform the buffer resizing
    FifoBuffer<AVFrame *, AVFramePtrContainer>::resize_locked(size);

    // if size increased and frame has been locked, set the new frames to match
    if (old_size > 0 && size > old_size)
    {
      FifoVector_t::iterator it = buffer.begin();
      if (it->unref_before_use)
        return;

      int W = it->data->width;
      int H = it->data->height;
      AVPixelFormat FMT = (AVPixelFormat)it->data->format;

      for (it += old_size; it < buffer.end(); it++)
      {
        // lock the frame buffer
        it->unref_before_use = false;

        // for convenience
        AVFrame *picture = it->data;

        // set all frames to as specified
        picture->width = W;
        picture->height = H;
        picture->format = FMT;
        av_frame_get_buffer(picture, 32);
      }
    }
  }
};
}
