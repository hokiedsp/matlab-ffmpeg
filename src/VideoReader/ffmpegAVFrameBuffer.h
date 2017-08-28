#pragma once

extern "C" {
#include <libavcodec/avcodec.h> // for AVFormatContext
}

#include <algorithm>

#include "ffmpegFiFoBuffer.h"
#include "ffmpegException.h"

namespace ffmpeg
{
typedef std::vector<AVFrame *> AVFramePtrVector;

class AVFramePtrBuffer : public FifoBuffer<AVFrame *>
{
 public:
   AVFramePtrBuffer(const uint32_t size = 2, const double timeout_s = 0.0) : FifoBuffer<AVFrame *>(0, timeout_s)
   {
      resize(size);
   }
   ~AVFramePtrBuffer()
   {
      resize(0);
   }

 protected:
   virtual void reset_internal()
   {
     // dereference queued packets
     for (int64_t i = rpos + 1; i < int64_t(wpos > rpos ? wpos + 1 : buffer.size()); i++)
       av_frame_unref(buffer[i]);
     if (wpos < rpos)
       for (int64_t i = 0; i <= wpos; i++)
         av_frame_unref(buffer[i]);

     // set wpos & rpos to -1
     FifoBuffer<AVFrame *>::reset_internal();
   }

   virtual void resize_internal(const uint32_t size_new)
   {
      // save old size
      size_t size_old = buffer.size();

      // dereference all the queued packets
      reset_internal();
      
      // if shrinks, deallocate av_frame first before letting go the buffer elements
      for (AVFramePtrVector::iterator it = buffer.begin() + size_new; it < buffer.end(); it++)
        av_frame_free(&*it);

      // resize buffer
      FifoBuffer<AVFrame *>::resize_internal(size_new);

      // create new AVPacket objects for each new buffer slot
      for (auto it = buffer.begin() + size_old; it < buffer.end(); it++)
      {
         *it = av_frame_alloc();
         if (*it == NULL)
            ffmpegException("Could not allocate memory for an AVPacket object.");
      }
   }
};
}
