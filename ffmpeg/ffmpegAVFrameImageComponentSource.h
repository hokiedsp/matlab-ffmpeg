#pragma once

#include "ffmpegAVFrameBufferBases.h" // import AVFrameSinkBase
#include "ffmpegException.h"          // import AVFrameSinkBase

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
class AVFrameImageComponentSource : public AVFrameSourceBase, protected VideoParams, virtual public IVideoHandler
{
public:
  AVFrameImageComponentSource()
      : AVFrameSourceBase(AVMEDIA_TYPE_VIDEO, {0, 1}), VideoParams({AV_PIX_FMT_NONE, 0, 0, {0, 0}}), desc(NULL),
        next_time(0), status(0), frame(NULL)
  {
  }

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
    clear();
  }

  using AVFrameSourceBase::getBasicMediaParams;

  const VideoParams &AVFrameImageComponentSource::getVideoParams() const { return (VideoParams &)*this; }

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
   * @param[in] params Image parameters (format, width, height, SAR)
   * @param[in] pdata Points to the frame component data buffer or NULL to end stream
   * @param[in] frame_offset First frame offset relative to the first frame in the buffer. The offset is given in frames.
   * @return Number of frame available in the returned pointers.
  */
  void load(const VideoParams &params, const uint8_t *pdata, uint64_t stride = 0)
  {
    // create new frame first without destroying current frame in case writing fails
    AVFrame *new_frame = NULL;

    // if data is present, create new AVFrame, else mark it eof
    if (pdata)
    {
      new_frame = av_frame_alloc();
      if (!new_frame)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::write] Could not allocate video frame.");
      new_frame->format = params.format;
      new_frame->width = params.width;
      new_frame->height = params.height;
      new_frame->sample_aspect_ratio = params.sample_aspect_ratio;
      if (av_frame_get_buffer(new_frame, 0) < 0)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::write] Could not allocate the video frame data.");
      if (av_frame_make_writable(new_frame) < 0)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::write] Could not make the video frame writable.");

      if (!stride)
        stride = width * height;
      for (int i = 0; i < desc->nb_components; ++i)
      {
        copy_component(pdata, desc->comp[i], new_frame);
        pdata += stride;
      }
    }

    // successfully created a new frame, ready to update the buffer
    std::unique_lock<std::mutex> l_tx(m);

    // if data already available, clear
    if (frame)
      av_frame_free(&frame);

    // if NULL, queue NULL to indicate the end of stream
    if (pdata)
    {
      frame = new_frame;
      *(VideoParams *)this = params;
      status = 1;
    }
    else
    {
      status = -1;
    }

    // notify that the frame data has been updated
    cv_tx.notify_one();
  }

  /**
 * \brief   Mark the source state to be EOF
 * 
 * \note EOF signal gets popped only once and object goes dormant
 */
  void markEOF()
  {
    std::unique_lock<std::mutex> l_tx(m);
    status = -1;
  }

protected:
  bool readyToPop_threadunsafe() const
  {
    // no room or eof
    return status != 0;
  }

  /**
   * \brief   Return a copy of the stored image as an AVFrame
   * 
   * The implementation for thread-safe AVFrameSource::pop() routine to return 
   * a copy of the stored image data as an AVFrame.
   * 
   * \note This function is called if and only if status!=0
   * 
   * \returns Populated AVFrame. Caller is responsible to free.
   */
  AVFrame *pop_threadunsafe()
  {
    AVFrame *outgoing_frame = NULL;
    if (status > 0) // if data available
    {
      AVFrameImageComponentSource::copy_frame(frame, outgoing_frame);
      outgoing_frame->pts = next_time++;
    }
    else //if (status < 0)
    {
      status = 0; // popped eof
    }
    return outgoing_frame;
  }

  /**
   * \brief   Clear stored frame data
   * 
   * The implementation for thread-safe AVFrameSource::clear() routine to erase
   * the stored image data.
   */
  void clear_threadunsafe()
  {
    // free all allocated AVFrames
    if (frame)
      av_frame_free(&frame);

    // clear the data parameters
    *(VideoParams *)this = {AV_PIX_FMT_NONE, 0, 0, {0, 0}};

    // reset the eof flag and write pointer positions
    status = 0;
    next_time = 0;
  }

private:
  /**
   * \brief Create a copy of a source AVFrame
   * 
   * \param[in] src Source AVFrame (must be populated with a valid Video AVFrame)
   * \param[out] dst Destination AVFrame. Its content will be overwritten, and 
   *                 allocated frame must be freed by the caller
   */
  static void copy_frame(const AVFrame *src, AVFrame *&dst)
  {
    dst = av_frame_alloc();
    if (!dst)
      throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::copy_queue] Could not allocate video frame.");
    dst->format = src->format;
    dst->width = src->width;
    dst->height = src->height;
    dst->sample_aspect_ratio = src->sample_aspect_ratio;
    if (av_frame_get_buffer(dst, 0) < 0)
      throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::copy_queue] Could not allocate the video frame data.");
    if (av_frame_make_writable(dst) < 0)
      throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::copy_queue] Could not make the video frame writable.");
    if (av_frame_copy(dst, src) < 0)
      throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::copy_queue] Could not copy the data from the source frame.");
  }

  /**
   * \brief Copy one component of a frame
   * 
   * \param[in] data   Points to the top of the current component data to create a new AVFrame
   * \param[in] d      Component descriptor of the current component
   * \param[out] frame Points to already prepared AVFrame to populate
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

  int64_t next_time; // increments after every pop
  int status;        // 0:not ready; >0:frame ready; <0:eof ready
  AVFrame *frame;    // source image data
};
}
