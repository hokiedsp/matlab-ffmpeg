#pragma once

#include "ffmpegAVFrameBufferBases.h" // import AVFrameSinkBase
#include "ffmpegException.h" // import AVFrameSinkBase

extern "C" {
#include <libavutil/pixfmt.h>  // import AVPixelFormat
#include <libavutil/pixdesc.h> // import AVPixFmtDescriptor

#include <libavutil/error.h>
#include <libavutil/rational.h>
#include <libavutil/frame.h>
}

namespace ffmpeg
{

/**
 * \brief An AVFrame sink for a video stream to store frames' component data
 *  An AVFrame sink, which converts a received video AVFrame to component data (e.g., RGB)
 */
class AVFrameImageComponentSource : public AVFrameSourceBase, public IVideoHandler, protected VideoParams
{
public:
  AVFrameImageComponentSource(const size_t w, const size_t h, const AVPixelFormat fmt, const AVRational &tb = {0, 1})
      : AVFrameSourceBase(AVMEDIA_TYPE_VIDEO, tb), VideoParams({(int)w,(int)h,{1,1},fmt}), desc(av_pix_fmt_desc_get(fmt)),
        next_time(0), status(0), frame(NULL)
  {
  }

  // default constructor => INVALID object
  AVFrameImageComponentSource() : AVFrameImageComponentSource(1, 1, AV_PIX_FMT_NONE){};

  // copy constructor
  AVFrameImageComponentSource(const AVFrameImageComponentSource &other)
      : AVFrameSourceBase(other), VideoParams(other), desc(other.desc),
        status(other.status), next_time(other.next_time)
  {
    AVFrameImageComponentSource::copy_frame(other.frame, frame);
  }

  // move constructor
  AVFrameImageComponentSource(AVFrameImageComponentSource &&other) noexcept
      : AVFrameSourceBase(other), VideoParams(other), desc(other.desc),
        status(other.status), frame(other.frame), next_time(other.next_time)
  {
    // reset other's data
    other.frame = NULL;
    other.next_time = 0;
    other.status = 0;
  }

  virtual ~AVFrameImageComponentSource()
  {
    reset();
  }

  const VideoParams& AVFrameImageComponentSource::getVideoParams() const { return (VideoParams&)*this; }

  void reset(const size_t nframes = 0, const AVPixelFormat fmt = AV_PIX_FMT_NONE) // must re-implement to allocate data_buf
  {
    std::unique_lock<std::mutex> l_tx(m);
    reset_threadunsafe(nframes, fmt);
  }

  /**
   * \brief Returns true if the data stream has reached end-of-file
   * 
   * Returns true if the queued data stream has reached end-of-file.
   * @return True if the last pushed AVFrame was an EOF marker.
   */
  bool eof()
  {
    // no room or eof
    std::unique_lock<std::mutex> l_tx(m);
    return status < 0;
  }

  /**
   * \brief Put new frame data into the buffer
   * 
   * Get direct-access pointers to the data or time buffers for data retrieval.
   * @param[in] pdata Points to the frame component data buffer or NULL to end stream
   * @param[in] frame_offset First frame offset relative to the first frame in the buffer. The offset is given in frames.
   * @return Number of frame available in the returned pointers.
  */
  void write(const uint8_t *pdata, uint64_t stride = 0)
  {
    AVFrame *new_frame=NULL;

    if (!pdata)
    {
      new_frame = av_frame_alloc();
      if (!new_frame)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::write] Could not allocate video frame.");
      new_frame->format = format;
      new_frame->width = width;
      new_frame->height = height;
      if (av_frame_get_buffer(new_frame, 0) < 0)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::write] Could not allocate the video frame data.");
      if (av_frame_make_writable(new_frame) < 0)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::write] Could not make the video frame writable.");

      if (!stride) stride = width * height;
      for (int i = 0; i < desc->nb_components; ++i)
      {
        copy_component(pdata, desc->comp[i], new_frame);
        pdata += stride;
      }
    }

    // successfully created a new frame, ready to update the buffer
    std::unique_lock<std::mutex> l_tx(m);

    // if data already available, clear
    if (frame) av_frame_free(&frame);

    // if NULL, queue NULL to indicate the end of stream
    if (pdata)
    {
      frame = new_frame;
      status = 1;
    }
    else
    {
      status = -1;
    }

    // notify that the frame data has been updated
    cv_tx.notify_one();
  }

protected:
  bool readyToPop_threadunsafe() const
  {
    // no room or eof
    return status != 0;
  }

  AVFrame *pop_threadunsafe()
  {
    AVFrame *outgoing_frame = NULL;
    AVFrameImageComponentSource::copy_frame(frame, outgoing_frame);
    outgoing_frame->pts = next_time++;
    if (status < 0)
      status = 0; // popped eof
    return outgoing_frame;
  }

  virtual void reset_threadunsafe(const size_t nframes, const AVPixelFormat fmt) // must re-implement to allocate data_buf
  {
    // free all allocated AVFrames
    if (frame) av_frame_free(&frame);

    // reset the eof flag and write pointer positions
    status = 0;
    if (fmt != AV_PIX_FMT_NONE)
    {
      format = fmt;
      desc = av_pix_fmt_desc_get(fmt);
    }
  }

private:
  /**
   * Copy buffer contents and write pointers AFTER other member variables are copied
   */
  static void copy_frame(const AVFrame *src, AVFrame *&dst)
  {
    dst = av_frame_alloc();
    if (!dst)
      throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::copy_queue] Could not allocate video frame.");
    dst->format = src->format;
    dst->width = src->width;
    dst->height = src->height;
    if (av_frame_get_buffer(dst, 0) < 0)
      throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::copy_queue] Could not allocate the video frame data.");
    if (av_frame_make_writable(dst) < 0)
      throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::copy_queue] Could not make the video frame writable.");
    if (av_frame_copy(dst, src) < 0)
      throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::copy_queue] Could not copy the data from the source frame.");
  }

  /**
   * Copy one component of a frame
  */
  static void copy_component(const uint8_t *data, const AVComponentDescriptor &d, AVFrame *frame)
  {
    int lnsz = frame->linesize[d.plane];
    uint8_t *dst = frame->data[d.plane];
    uint8_t *dst_end = dst + frame->height * lnsz;
    // Copy frame data
    for (; dst < dst_end; dst += lnsz) // for each line
    {
      uint8_t *line = dst + d.offset;
      for (int w = 0; w < frame->width; ++w) // for each column
      {
        // get the data
        *line = (*(data++)) << d.shift;

        // go to next pixel
        line += d.step;
      }
    }
  }

  const AVPixFmtDescriptor *desc;

  int64_t next_time;

  int status; // 0:not ready; >0:frame ready; <0:eof ready
  AVFrame * frame;
};
}
