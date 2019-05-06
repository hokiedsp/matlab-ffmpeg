#pragma once

#include "ffmpegAVFrameBufferBases.h" // import AVFrameSinkBase
#include "ffmpegImageUtils.h"

#include "ffmpegLogUtils.h"

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
template <class Allocator = ffmpegAllocator<uint8_t>(), class Mutex_t = std::shared_mutex>
class AVFrameVideoComponentSink : public AVFrameSinkBase<Mutex_t>, public VideoHandler
{
public:
  AVFrameVideoComponentSink(const AVRational &tb = {0, 0})
      : AVFrameSinkBase(AVMEDIA_TYPE_VIDEO, tb),
        nb_frames(1), time_buf(NULL), frame_data_sz(0), data_buf(NULL), has_eof(false), wr_time(NULL), wr_data(NULL)
  {
  }

  // copy constructor
  AVFrameVideoComponentSink(const AVFrameVideoComponentSink &other)
      : AVFrameSinkBase(other), VideoHandler(other),
        nb_frames(other.nb_frames), frame_data_sz(other.frame_data_sz), has_eof(other.has_eof)
  {
    copy_buffers(other);
  }

  // move constructor
  AVFrameVideoComponentSink(AVFrameVideoComponentSink &&other) noexcept
      : AVFrameSinkBase(other), VideoHandler(other), nb_frames(other.nb_frames),
        time_buf(other.time_buf), frame_data_sz(other.frame_data_sz), data_buf(other.data_buf),
        has_eof(other.has_eof), wr_time(other.wr_time), wr_data(other.wr_data)
  {
    // reset other
    other.nb_frames = 0;
    other.time_buf = NULL;
    other.data_buf = NULL;
    other.wr_time = NULL;
    other.wr_data = NULL;
  }

  virtual ~AVFrameVideoComponentSink()
  {
    av_log(NULL, AV_LOG_INFO, "destroying AVFrameVideoComponentSink\n");
    std::unique_lock<Mutex_t> l_rx(m);
    if (time_buf)
    {
      allocator.deallocate((uint8_t *)time_buf, nb_frames * sizeof(double));
      allocator.deallocate(data_buf, nb_frames * width * height);
    }
    av_log(NULL, AV_LOG_INFO, "destroyed AVFrameVideoComponentSink\n");
  }

  bool supportedFormat(int format) const
  {
    // must <= 8-bit/component
    return format != AV_PIX_FMT_NONE ? imageCheckComponentSize((AVPixelFormat)format) : false;
  }

  /**
 * \brief   Reset buffer-size
 */
  void reset(const size_t nframes = 0) // must re-implement to allocate data_buf
  {
    std::unique_lock<Mutex_t> l_rx(m);
    reset_threadunsafe(nframes);
    if (nframes) //notify the reader for the buffer availability
      cv_rx.notify_one();
  }

  /**
 * \brief   Release buffers and reallocate if so requested
 */
  size_t release(uint8_t **data, int64_t **time = NULL, bool reallocate = true)
  {
    std::unique_lock<Mutex_t> l_rx(m);
    size_t rval = wr_time - time_buf; // save the # of frames in releasing buffer

    if (data)
      *data = data_buf;
    if (time)
      *time = time_buf;

    data_buf = wr_data = NULL;
    time_buf = wr_time = NULL;

    // reallocate memory for the buffer
    if (reallocate)
    {
      reallocate_threadunsafe();
      if (nb_frames) //notify the reader for the buffer availability
        cv_rx.notify_one();
    }

    return rval; // number of frames
  }

  /**
   * \brief Returns true if the data stream has reached end-of-file
   * 
   * Returns true if the data stream has reached end-of-file.
   * @return True if the last pushed AVFrame was an EOF marker.
   */
  bool eof_threadunsafe() const
  {
    // no room or eof
    return has_eof; // true if last
  }

  /**
   * \brief Get pointers to the buffers
   * 
   * Get direct-access pointers to the data or time buffers for data retrieval.
   * @param pdata [out] Points to the frame data buffer or NULL if requested invalid frame
   * @param ptime [out] Points to the frame time buffer or NULL if requested invalid frame
   * @param frame_offset [in] First frame offset relative to the first frame in the buffer. The offset is given in frames.
   * @return Number of frame available in the returned pointers.
  */
  int read(const uint8_t *&pdata, const int64_t *&ptime, const size_t frame_offset = 0)
  {
    std::shared_lock<Mutex_t> l_rx(m);

    size_t data_sz = wr_time - time_buf;
    if (frame_offset < data_sz && data_sz > 0)
    {
      ptime = time_buf + frame_offset;
      pdata = data_buf + frame_offset * frame_data_sz;
      return wr_time - ptime;
    }
    else
    {
      ptime = NULL;
      pdata = NULL;
      return 0;
    }
  }

  /**
   * \brief Get pointers to the buffers
   * 
   * Get direct-access pointers to the data or time buffers for data retrieval.
   * @param pdata [out] Points to the frame data buffer or NULL if requested invalid frame
   * @param frame_offset [in] First frame offset relative to the first frame in the buffer. The offset is given in frames.
   * @return Number of frame available in the returned pointers.
  */
  int read(const uint8_t *&pdata, const size_t frame_offset = 0)
  {
    std::shared_lock<Mutex_t> l_rx(m);
    size_t data_sz = wr_time - time_buf;
    if (frame_offset < data_sz && data_sz > 0)
    {
      pdata = data_buf + frame_offset * frame_data_sz;
      return wr_time - ptime;
    }
    else
    {
      pdata = NULL;
      return 0;
    }
  }

  /**
   * \brief Get pointers to the buffers
   * 
   * Get direct-access pointers to the data or time buffers for data retrieval.
   * @param ptime [out] Points to the frame time buffer or NULL if requested invalid frame
   * @param frame_offset [in] First frame offset relative to the first frame in the buffer. The offset is given in frames.
   * @return Number of frame available in the returned pointers.
  */
  int read_time(const int64_t *&ptime, const size_t frame_offset = 0)
  {
    std::shared_lock<Mutex_t> l_rx(m);
    size_t data_sz = wr_time - time_buf;
    if (frame_offset < data_sz && data_sz > 0)
    {
      ptime = time_buf + frame_offset;
      return wr_time - ptime;
    }
    else
    {
      ptime = NULL;
      return 0;
    }
  }

protected:
  bool readyToPush_threadunsafe() const // declared in AVFrameSinkBase
  {
    // no room or eof
    return !(has_eof && time_buf && (time_buf + nb_frames >= wr_time + frame_data_sz));
  }

  // safe to assume object is ready to accept a new frame
  int push_threadunsafe(AVFrame *frame)
  {
    av_log(NULL, AV_LOG_INFO, "in push_threadunsafe\n");
    if (frame)
    {
      av_log(NULL, AV_LOG_INFO, "received a frame\n");
      // if buffer format has not been set or frame parameters changed, reallocate the buffer
      if (!time_buf || frame->format != format || frame->width != width || frame->height != height)
      {
        av_log(NULL, AV_LOG_INFO, "need to reallocate buffer\n");
        setVideoParams(VideoParams({(AVPixelFormat)frame->format, frame->width, frame->height, frame->sample_aspect_ratio}));
        reallocate_threadunsafe();
        av_log(NULL, AV_LOG_INFO, "allocated %d bytes\n", frame_data_sz);
      }

      // copy time
      if (frame->pts != AV_NOPTS_VALUE)
        *wr_time = frame->pts;
      else
        *wr_time = -1;
      av_log(NULL, AV_LOG_INFO, "pts=%d\n", *wr_time);
      ++wr_time;

      logPixelFormat(av_pix_fmt_desc_get((AVPixelFormat)frame->format), "push_threadunsafe");

      // copy data
      wr_data += imageCopyToComponentBuffer(wr_data, (int)frame_data_sz, frame->data, frame->linesize,
                                            (AVPixelFormat)frame->format, frame->width, frame->height);
      av_log(NULL, AV_LOG_INFO, "image data (%d bytes) written\n", wr_data - data_buf);
    }
    else // null frame == eof marker
    {
      av_log(NULL, AV_LOG_INFO, "received an eof marker\n");
      has_eof = true;
    }
    return 0;
  }

  virtual bool clear_threadunsafe(const bool deep)
  {
    if (deep) // clears expected frame size & frees the buffers
    {
      *(VideoParams *)this = {AV_PIX_FMT_NONE, 0, 0, {0, 0}};
      reallocate_threadunsafe();
    }
    else
    { // clear eof flag and reset write pointers
      has_eof = false;
      wr_time = time_buf;
      wr_data = data_buf;
    }
    return data_buf; // if data_buf is non-null, ready to fill more
  }

  void deallocate_threadunsafe()
  {
  }

  void reallocate_threadunsafe()
  {
    // determine the data buffer size
    frame_data_sz = imageGetComponentBufferSize(format, width, height);

    // reallocate
    time_buf = (int64_t *)allocator.allocate(nb_frames * sizeof(int64_t), (uint8_t *)time_buf);
    data_buf = allocator.allocate(nb_frames * frame_data_sz, data_buf);

    // reset the eof flag and write pointer positions
    has_eof = false;
    wr_time = time_buf;
    wr_data = data_buf;
  }

  virtual void reset_threadunsafe(const size_t nframes) // must re-implement to allocate data_buf
  {
    // get new buffer size if specified
    // if nframes==0, keep the same capacity

    // reallocate only if new buffer size or previous memory was released
    if (nframes > 0 && nb_frames != nframes)
    {
      nb_frames = nframes;
      reallocate_threadunsafe();
    }
  }

private:
  /**
   * Copy buffer contents and write pointers AFTER other member variables are copied
   */
  void copy_buffers(const AVFrameVideoComponentSink &other)
  {
    // allocate buffers
    size_t data_sz = other.wr_time - other.time_buf; // number of frames currently in the buffer
    time_buf = (int64_t *)allocator.allocate(nb_frames * sizeof(int64_t));
    data_buf = allocator.allocate(nb_frames * frame_data_sz);

    // populate time buffer
    std::copy_n(other.time_buf, data_sz, time_buf);
    wr_time = time_buf + data_sz;

    // populate data buffer
    data_sz *= frame_data_sz; // convert to bytes from frames
    std::copy_n(other.data_buf, data_sz, data_buf);
    wr_data = data_buf + data_sz;
  }

  Allocator allocator;

  size_t frame_data_sz; // size of each frame in number of bytes

  size_t nb_frames;
  bool has_eof;

  int64_t *time_buf;
  uint8_t *data_buf; // MUST BE SET BY IMPLEMENTATION

  int64_t *wr_time;
  uint8_t *wr_data;
};
}
