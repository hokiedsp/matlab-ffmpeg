#pragma once

extern "C" {
#include <libavcodec/avcodec.h> // for AVFormatContext
}

#include "ffmpegFiFoBuffer.h"
#include "ffmpegException.h"

namespace ffmpeg
{
typedef std::vector<AVFrame *> AVFramePtrVector;

class AVFramePtrBuffer : public FifoBuffer<AVFrame *>
{
 public:
   AVFramePtrBuffer(const int size = 2, const double timeout_s = 0.0) : FifoBuffer<AVFrame *>(size, timeout_s) {}
   ~AVFramePtrBuffer()
   {
      reset();
   }

 protected:
   virtual void reset_internal()
   {
      // set wpos & rpos to -1
      FifoBuffer<AVFrame *>::reset_internal();

      // dereference queued packets
      for (int i = rpos + 1; i < (wpos > rpos ? wpos + 1 : buffer.size()); i++)
         av_frame_unref(buffer[i]);
      if (wpos < rpos)
         for (int i = 0; i <= wpos; i++)
            av_frame_unref(buffer[i]);
   }

   virtual void resize_internal(const int size_new)
   {
      // save old size
      size_t size_old = buffer.size();

      // dereference queued packets
      reset_internal();

      // if shrinks, deallocate
      if (size_new < size_old)
         for (AVFramePtrVector::iterator it = buffer.begin() + size_old; it < buffer.end(); it++)
            av_frame_free(&*it);

      // resize buffer
      FifoBuffer<AVFrame *>::resize_internal(size_new);

      // create new AVPacket objects for each new buffer slot
      for (AVFramePtrVector::iterator it = buffer.begin() + size_old; it < buffer.end(); it++)
      {
         *it = av_frame_alloc();
         if (*it == NULL)
            ffmpegException("Could not allocate memory for an AVPacket object.");
      }
   }
};
}
