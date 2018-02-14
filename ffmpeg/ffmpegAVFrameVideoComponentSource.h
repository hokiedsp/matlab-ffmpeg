#pragma once

#include "ffmpegAVFrameBufferBases.h" // import AVFrameSinkBase
#include "ffmpegImageUtils.h"

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
class AVFrameVideoComponentSource : public AVFrameSourceBase, public VideoHandler
{
public:
  AVFrameVideoComponentSource(const size_t w, const size_t h, const AVPixelFormat fmt, const AVRational &tb = {0, 0})
      : AVFrameSourceBase(AVMEDIA_TYPE_VIDEO, tb), VideoHandler(w, h, {1, 1}, fmt), desc(av_pix_fmt_desc_get(fmt)), nb_frames(-1),
        next_time(0), has_eof(false)
  {
  }

  // default constructor => INVALID object
  AVFrameVideoComponentSource() : AVFrameVideoComponentSource(1, 1, AV_PIX_FMT_NONE){};

  // copy constructor
  AVFrameVideoComponentSource(const AVFrameVideoComponentSource &other)
      : AVFrameSourceBase(other), VideoHandler(other),
        nb_frames(other.nb_frames), has_eof(other.has_eof), next_time(other.next_time)
  {
    copy_queue(other);
  }

  // move constructor
  AVFrameVideoComponentSource(AVFrameVideoComponentSource &&other) noexcept
      : AVFrameSourceBase(other), VideoHandler(other), time_base(other.time_base), nb_frames(other.nb_frames),
        has_eof(other.has_eof), frame_queue(other.frame_queue), next_time(other.next_time)
  {
    // reset other's data
    other.frame_queue.clear();
    other.next_time = 0;
    other.has_eof = false;
  }

  virtual ~AVFrameVideoComponentSource()
  {
    reset();
  }

  bool supportedFormat(int format) const
  {
    return format != AV_PIX_FMT_NONE ? imageCheckComponentSize((AVPixelFormat)format) : false;
  }

  bool ready() const
  {
    return AVFrameSourceBase::ready() && VideoHandler::ready();
  }

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
    return has_eof; // true if last
  }

  /**
   * \brief Put new frame data into the buffer
   * 
   * Get direct-access pointers to the data or time buffers for data retrieval.
   * @param[in] pdata  Points to the frame component data buffer or NULL to end stream
   * @param[in] stride [optional] Number of bytes allocated for each component. Default: height*width
   * @return Current total of frames written
  */
  int64_t write(const uint8_t *pdata, const int pdata_size, const int linesize = 0, const int compsize = 0)
  {
    if (has_eof)
      throw ffmpegException("[ffmpeg::AVFrameVideoComponentSource::write] Cannot add any more frames as end-of-stream has already been marked.");
    std::unique_lock<std::mutex> l_tx(m);
    if (frame_queue.size() == nb_frames)
      throw ffmpegException("[ffmpeg::AVFrameVideoComponentSource::write] Frame buffer is full.");
    l_tx.unlock();

    AVFrame *new_frame = NULL;
    if (pdata)
    {
      new_frame = av_frame_alloc();
      if (!new_frame)
        throw ffmpegException("[ffmpeg::AVFrameVideoComponentSource::write] Could not allocate video frame.");
      new_frame->format = pixfmt;
      new_frame->width = width;
      new_frame->height = height;
      if (av_frame_get_buffer(new_frame, 0) < 0)
        throw ffmpegException("[ffmpeg::AVFrameVideoComponentSource::write] Could not allocate the video frame data.");
      if (av_frame_make_writable(new_frame) < 0)
        throw ffmpegException("[ffmpeg::AVFrameVideoComponentSource::write] Could not make the video frame writable.");

      imageCopyFromComponentBuffer(pdata, pdata_size, new_frame->data, new_frame->linesize,
                                   (AVPixelFormat)new_frame->format, new_frame->width, new_frame->height,
                                   linesize, compsize);
      new_frame->pts = next_time++;
    }

    // new frame created, ready to push the frame to the queue
    l_tx.lock();

    // if NULL, queue NULL to indicate the end of stream
    queue.push_back(new_frame);
    has_eof = true;
    cv_tx.notify_one();
    return next_time;
  }

protected:
  bool readyToPop_threadunsafe() const
  {
    // no room or eof
    return !has_eof || nb_frames >= (rd_time - time_buf);
  }
  void pop_threadunsafe(AVFrame *frame)
  {
    AVFrame *rval = frame_queue.front();
    frame_queue.pop_front();

    av_frame_unref(frame);
    av_frame_move_ref(frame, rval);
    av_frame_free(rval);

    return rval;
  }

  virtual void reset_threadunsafe(const size_t nframes, const AVPixelFormat fmt) // must re-implement to allocate data_buf
  {
    // free all allocated AVFrames
    std::for_each(frame_queue.begin(), frame_queue.end(), [](AVFrame *frame) {if (frame) av_frame_free(frame); });
    frame_queue.clear();

    // reset the eof flag and write pointer positions
    has_eof = false;
    if (nframes != 0)
      nb_frames = nframes;
    if (fmt != AV_PIX_FMT_NONE)
    {
      pixfmt = fmt;
      desc = av_pix_fmt_desc_get(pixfmt);
    }
  }

private:
  /**
   * Copy buffer contents and write pointers AFTER other member variables are copied
   */
  void copy_queue(const AVFrameVideoComponentSource &other)
  {
    // clear the buffer
    reset_threadunsafe();

    std::for_each(other.frame_queue.begin(), other.frame_queue.end(), [&](const AVFrame *src) {
      AVFrame *frame = av_frame_alloc();
      if (!frame)
        throw ffmpegException("[ffmpeg::AVFrameVideoComponentSource::copy_queue] Could not allocate video frame.");
      frame->format = src->pixfmt;
      frame->width = src->width;
      frame->height = src->height;
      if (av_frame_get_buffer(frame, 0) < 0)
        throw ffmpegException("[ffmpeg::AVFrameVideoComponentSource::copy_queue] Could not allocate the video frame data.");
      if (av_frame_make_writable(frame) < 0)
        throw ffmpegException("[ffmpeg::AVFrameVideoComponentSource::copy_queue] Could not make the video frame writable.");
      if (av_frame_copy(frame, src) < 0)
        throw ffmpegException("[ffmpeg::AVFrameVideoComponentSource::copy_queue] Could not copy the data from the source frame.");
    })
  }

  AVPixelFormat pixfmt;
  const AVPixFmtDescriptor *desc;

  size_t width;
  size_t height;

  size_t nb_frames;
  bool has_eof;

  int64_t next_time;
  std::deque<AVFrame *> frame_queue;
};
}
