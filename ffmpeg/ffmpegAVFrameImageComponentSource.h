#pragma once

#include "ffmpegAVFrameBufferBases.h" // import AVFrameSinkBase
#include "ffmpegException.h"          // import AVFrameSinkBase
#include "ffmpegImageUtils.h"

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
class AVFrameImageComponentSource : public AVFrameSourceBase, public IVideoHandler
{
public:
  AVFrameImageComponentSource()
      : AVFrameSourceBase(AVMEDIA_TYPE_VIDEO, AVRational({1, 1})), desc(NULL),
        next_time(0), status(0)
  {
    frame = av_frame_alloc();
    if (!frame)
      throw ffmpegException("[ffmpeg::filter::AVFrameImageComponentSource]Failed to allocate AVFrame.");

    av_log(NULL, AV_LOG_INFO, "[ffmpeg::AVFrameImageComponentSource:default] time_base:1/1->%d/%d\n", time_base.num, time_base.den);
    av_log(NULL, AV_LOG_INFO, "[ffmpeg::AVFrameImageComponentSource:default] mediatype:%s->%s\n", av_get_media_type_string(AVMEDIA_TYPE_VIDEO), av_get_media_type_string(type));
  }

  // copy constructor
  AVFrameImageComponentSource(const AVFrameImageComponentSource &other)
      : AVFrameSourceBase(other), desc(other.desc),
        status(other.status), next_time(other.next_time)
  {
    frame = av_frame_clone(other.frame);
    if (!frame)
      throw ffmpegException("[ffmpeg::filter::AVFrameImageComponentSource]Failed to clone AVFrame.");
    if (av_frame_make_writable(frame) < 0)
      throw ffmpegException("[ffmpeg::filter::AVFrameImageComponentSource]Failed to make AVFrame writable.");
  }

  // move constructor
  AVFrameImageComponentSource(AVFrameImageComponentSource &&other) noexcept
      : AVFrameSourceBase(other), desc(other.desc), frame(other.frame), status(other.status),
        next_time(other.next_time)
  {
    other.frame = av_frame_alloc();
  }

  virtual ~AVFrameImageComponentSource()
  {
    av_log(NULL, AV_LOG_INFO, "destroying AVFrameImageComponentSource\n");
    av_frame_free(&frame);
    av_log(NULL, AV_LOG_INFO, "destroyed AVFrameImageComponentSource\n");
  }

  bool validVideoParams() const
  {
    return ((AVPixelFormat)frame->format != AV_PIX_FMT_NONE) &&
           frame->width > 0 && frame->height > 0 &&
           frame->sample_aspect_ratio.num != 0 && frame->sample_aspect_ratio.den != 0;
  }

  // implement IVideoHandler functions
  VideoParams
  getVideoParams() const
  {
    return VideoParams({(AVPixelFormat)frame->format, frame->width, frame->height, frame->sample_aspect_ratio});
  }
  AVPixelFormat getFormat() const { return (AVPixelFormat)frame->format; }
  std::string getFormatName() const { return ((AVPixelFormat)frame->format != AV_PIX_FMT_NONE) ? av_get_pix_fmt_name((AVPixelFormat)frame->format) : ""; }
  int getWidth() const { return frame->width; }
  int getHeight() const { return frame->height; }
  AVRational getSAR() const { return frame->sample_aspect_ratio; }

  void setVideoParams(const VideoParams &params)
  {
    bool critical_change = frame->format != (int)params.format && frame->width != params.width && frame->height != params.height;

    // if no parameters have changed, exit
    if (!(critical_change && av_cmp_q(frame->sample_aspect_ratio, params.sample_aspect_ratio)))
      return;

    // if data critical parameters have changed, free frame data
    if (critical_change)
      release_frame();

    // copy new parameter values
    frame->format = (int)params.format;
    frame->width = params.width;
    frame->height = params.height;
    frame->sample_aspect_ratio = params.sample_aspect_ratio;
  }
  void setVideoParams(const IVideoHandler &other) { setVideoParams(other.getVideoParams()); }

  void setFormat(const AVPixelFormat fmt)
  {
    if (frame->format == (int)fmt)
      return;
    release_frame();
    frame->format = (int)fmt;
  }
  void setWidth(const int w)
  {
    if (frame->width == w)
      return;
    release_frame();
    frame->width = w;
  }
  void setHeight(const int h)
  {
    if (frame->height == h)
      return;
    release_frame();
    frame->height = h;
  }
  void setSAR(const AVRational &sar)
  {
    if (!av_cmp_q(frame->sample_aspect_ratio, sar))
      return;
    frame->sample_aspect_ratio = sar;
  }

  ///////////////////////////////////////////////////////////////////////

  bool ready() const
  {
    return AVFrameSourceBase::ready() && status != 0;
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
  void load(const VideoParams &params, const uint8_t *pdata, const int pdata_size, const int linesize = 0, const int compsize = 0)
  {
    // create new frame first without destroying current frame in case writing fails
    // if data is present, create new AVFrame, else mark it eof
    if (pdata)
    {
      int total_size = imageGetComponentBufferSize(params.format, params.width, params.height);
      if (!total_size)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::load] Critical image parameters missing.");
      if (pdata_size < total_size)
        throw ffmpegException("[ffmpeg::AVFrameImageComponentSource::load] Not enough data given to fill the image buffers.");

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
    // save the image parameters
    VideoParams params = getVideoParams();

    // release the FFmpeg frame buffers
    av_frame_unref(frame);

    // copy back image parameters, cannot use setVideoParams() because it fires release_frame() recursively
    frame->format = (int)params.format;
    frame->width = params.width;
    frame->height = params.height;
    frame->sample_aspect_ratio = params.sample_aspect_ratio;

    status = 0;
  }

  AVFrame *frame;
  const AVPixFmtDescriptor *desc;

  int64_t next_time; // increments after every pop
  int status;        // 0:not ready; >0:frame ready; <0:eof ready
};
}
