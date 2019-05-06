#pragma once

#include "ffmpegAVFrameBufferBases.h" // import AVFrameSinkBase
#include "ffmpegException.h"          // import AVFrameSinkBase
#include "ffmpegImageUtils.h"

#include "ffmpegLogUtils.h"

extern "C" {
#include <libavutil/pixfmt.h>  // import AVPixelFormat
#include <libavutil/pixdesc.h> // import AVPixFmtDescriptor

#include <libavutil/error.h>
#include <libavutil/rational.h>
#include <libavutil/frame.h>
}

#include <memory>

namespace ffmpeg
{

/**
 * \brief An AVFrame sink for a video stream to store frames' component data
 *  An AVFrame sink, which converts a received video AVFrame to component data (e.g., RGB)
 */
class AVFrameImageComponentSource : public AVFrameSourceBase, public VideoAVFrameHandler
{
public:
  AVFrameImageComponentSource()
      : AVFrameSourceBase(AVMEDIA_TYPE_VIDEO, AVRational({1, 1})),
        next_time(0), status(0) {}

  // copy constructor
  AVFrameImageComponentSource(const AVFrameImageComponentSource &other)
      : AVFrameSourceBase(other), VideoAVFrameHandler(other),
        status(other.status), next_time(other.next_time)  {}

  // move constructor
  AVFrameImageComponentSource(AVFrameImageComponentSource &&other) noexcept
      : AVFrameSourceBase(other), VideoAVFrameHandler(other), status(other.status),
        next_time(other.next_time) {}

  virtual ~AVFrameImageComponentSource()
  {
    av_log(NULL, AV_LOG_INFO, "destroyed AVFrameImageComponentSource\n");
  }

  bool supportedFormat(int format) const
  {
    return format != AV_PIX_FMT_NONE ? imageCheckComponentSize((AVPixelFormat)format) : false;
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
   * Place the new image data onto the AVFrame buffer. This includes the byte-array data as well
   * as the video frame parameeter.
   * 
   * @param[in] params Image parameters (format, width, height, SAR)
   * @param[in] pdata Points to the frame component data buffer or NULL to end stream
   * @param[in] frame_offset First frame offset relative to the first frame in the buffer. The offset is given in frames.
   * @return Number of frame available in the returned pointers.
  */
  void load(VideoParams params, const uint8_t *pdata, const int pdata_size, const int linesize = 0, const int compsize = 0)
  {
    // create new frame first without destroying current frame in case writing fails
    // if data is present, create new AVFrame, else mark it eof
    if (pdata)
    {
      // overwrite any invalid params values with the object's value
      if (params.format == AV_PIX_FMT_NONE)
        params.format = (AVPixelFormat)frame->format;
      if (params.width <= 0)
        params.width = frame->width;
      if (params.height <= 0)
        params.height = frame->height;
      if (av_cmp_q(params.sample_aspect_ratio, {0, 0}))
        params.sample_aspect_ratio = frame->sample_aspect_ratio;

      int total_size = imageGetComponentBufferSize(params.format, params.width, params.height);
      if (!total_size)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::load] Critical image parameters missing.");
      if (pdata_size < total_size)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::load] Not enough data (%d bytes) given to fill the image buffers (%d bytes).", pdata_size, total_size);

      auto avFrameFree = [](AVFrame *frame) { av_frame_free(&frame); };
      std::unique_ptr<AVFrame, decltype(avFrameFree)> new_frame(av_frame_alloc(), avFrameFree);
      if (!new_frame)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::load] Could not allocate video frame.");
      new_frame->format = params.format;
      new_frame->width = params.width;
      new_frame->height = params.height;
      new_frame->sample_aspect_ratio = params.sample_aspect_ratio;

      if (av_frame_get_buffer(new_frame.get(), 0) < 0)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::load] Could not allocate the video frame data.");
      if (av_frame_make_writable(new_frame.get()) < 0)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::load] Could not make the video frame writable.");

      imageCopyFromComponentBuffer(pdata, pdata_size, new_frame->data, new_frame->linesize,
                                   (AVPixelFormat)new_frame->format, new_frame->width, new_frame->height,
                                   linesize, compsize);

      // successfully created a new frame, ready to update the buffer
      std::unique_lock<std::mutex> l_tx(m);

      // if NULL, queue NULL to indicate the end of stream
      av_frame_unref(frame);
      av_frame_move_ref(frame, new_frame.get());
      status = av_cmp_q(frame->sample_aspect_ratio, {0, 0}) ? 1 : 0; // if data filled,
    }
    else
    {
      std::unique_lock<std::mutex> l_tx(m);
      av_frame_unref(frame);
      status = -1;
    }

    // notify that the frame data has been updated
    cv_tx.notify_one();
  }

  /**
   * \brief Put new frame data into the buffer
   * 
   * Place the new image data onto the AVFrame buffer. This function takes just the data
   * and assumes the video parameters are already loaded correctly.
   * 
   * @param[in] pdata Points to the frame component data buffer or NULL to end stream
   * @param[in] frame_offset First frame offset relative to the first frame in the buffer. The offset is given in frames.
   * @return Number of frame available in the returned pointers.
  */
  void load(const uint8_t *pdata, const int pdata_size, const int linesize = 0, const int compsize = 0)
  {
    load(getVideoParams(), pdata, pdata_size, linesize, compsize);
  }

  /**
 * \brief   Mark the source state to be EOF
 * 
 * \note EOF signal gets popped only once and object goes dormant
 */
  void
  markEOF()
  {
    std::unique_lock<std::mutex> l_tx(m);
    status = -1;
    av_frame_unref(frame);
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
  void pop_threadunsafe(AVFrame *outgoing_frame)
  {
    if (status > 0) // if data available
    {
      if (av_frame_ref(outgoing_frame, frame) < 0)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::pop_threadunsafe] Failed to copy AVFrame.");
      outgoing_frame->pts = next_time++;
    }
    else //if (status < 0)
    {
      status = 0; // popped eof
    }
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
      av_frame_unref(frame);

    // reset the eof flag and write pointer positions
    status = 0;
    next_time = 0;
  }

private:
  /**
   * \brief Reallocate object's AVFrame 
   * 
   * release_frame() unreferences the existing frame but maintains
   * the parameter values.
   * 
   */
  void release_frame()
  {
    VideoAVFrameHandler::release_frame();
    status = 0;
  }

  int64_t next_time; // increments after every pop
  int status;        // 0:not ready; >0:frame ready; <0:eof ready
};
}
