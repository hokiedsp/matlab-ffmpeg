#pragma once

#include "ffmpegAVFrameBufferBases.h" // import AVFrameSinkBase

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
class AVFrameVideoComponentSink : public AVFrameSinkBase
{
public:
  AVFrameVideoComponentSink(const size_t w, const size_t h, const AVPixelFormat fmt, const AVRational &tb = {0, 0})
      : AVFrameSinkBase(tb), pixfmt(fmt), desc(av_pix_fmt_desc_get(fmt)), nb_frames(0), width(w), height(h),
        time_buf(NULL), frame_data_sz(0), data_buf(NULL), has_eof(false), wr_time(NULL), wr_data(NULL)
  {
  }

  AVFrameVideoComponentSink(const AVFrame *frame, const AVRational &tb = {0, 0})
      : AVFrameVideoComponentSink(frame->width, frame->height, (AVPixelFormat)frame->format, tb)
  {
  }

  // default constructor => INVALID object
  AVFrameVideoComponentSink() : AVFrameVideoComponentSink(1, 1, AV_PIX_FMT_NONE){};

  // copy constructor
  AVFrameVideoComponentSink(const AVFrameVideoComponentSink &other)
      : AVFrameSinkBase(other), width(other.width), height(other.height), pixfmt(other.pixfmt),
        nb_frames(other.nb_frames), frame_data_sz(other.frame_data_sz), has_eof(other.has_eof)
  {
    copy_buffers(other);
  }

  // move constructor
  AVFrameVideoComponentSink(AVFrameVideoComponentSink &&other) noexcept
      : width(other.width), height(other.height), pixfmt(other.pixfmt), time_base(other.time_base), nb_frames(other.nb_frames),
        time_buf(other.time_buf), frame_data_sz(other.frame_data_sz), data_buf(other.data_buf),
        has_eof(other.has_eof), wr_time(other.wr_time), wr_data(other.wr_data)
  {
    // reset other
    other.pixfmt = AV_PIX_FMT_NONE;
    other.time_base = {0, 0};
    other.nb_frames = 0;
    other.time_buf = NULL;
    other.data_buf = NULL;
    other.wr_time = NULL;
    other.wr_data = NULL;
  }

  // copy assign operator
  virtual AVFrameVideoComponentSink &operator=(const AVFrameBufferBase &right)
  {
    std::unique_lock<std::mutex> l_rx(m);

    const AVFrameVideoComponentSink *other = dynamic_cast<const AVFrameVideoComponentSink *>(&right);
    AVFrameSinkBase::operator()(other);
    width = other.width;
    height = other.height;
    pixfmt = other.pixfmt;
    nb_frames = other.nb_frames;
    frame_data_sz = other.frame_data_sz;
    has_eof = other.has_eof;

    copy_buffers(other);

    return *this;
  }

  // move assign operator
  virtual AVFrameVideoComponentSink &operator=(AVFrameBufferBase &&right)
  {
    std::unique_lock<std::mutex> l_rx(m);

    AVFrameVideoComponentSink *other = dynamic_cast<const AVFrameVideoComponentSink *>(&right);
    AVFrameSinkBase::operator()(other);

    width = other.width;
    height = other.height;
    pixfmt = other.pixfmt;
    nb_frames = other.nb_frames;
    frame_data_sz = other.frame_data_sz;
    has_eof = other.has_eof;

    time_buf = other.time_buf;
    data_buf = other.data_buf;
    wr_time = other.wr_time;
    wr_data = other.wr_data;

    // reset other
    other.pixfmt = AV_PIX_FMT_NONE;
    other.time_base = {0, 0};
    other.nb_frames = 0;
    other.time_buf = NULL;
    other.data_buf = NULL;
    other.wr_time = NULL;
    other.wr_data = NULL;

    return *this;
  }

  virtual ~AVFrameVideoComponentSink()
  {
    std::unique_lock<std::mutex> l_rx(m);
    allocator.deallocate((uint8_t *)time_buf, nb_frames * sizeof(double));
    allocator.deallocate(data_buf, data_sz);
  }

  AVMediaType getMediaType() const { return AVMEDIA_TYPE_VIDEO; };
    
  void reset(const size_t nframes = 0) // must re-implement to allocate data_buf
  {
    std::unique_lock<std::mutex> l_rx(m);
    reset_threadunsafe(nframes);
  }

  size_t release(uint8_t **data, int64_t **time = NULL, bool reallocate = true)
  {
    std::unique_lock<std::mutex> l_rx(m);
    size_t rval = wr_time - time_buf; // save the # of frames in releasing buffer

    if (data)
      *data = data_buf;
    if (time)
      *time = time_buf;

    // reallocate memory for the buffer
    if (reallocate)
    {
      reset_threadunsafe(0);
    }
    else
    {
      data_buf = wr_data = NULL;
      time_buf = wr_time = NULL;
    }

    return rval; // number of frames
  }


  /**
   * \brief Returns true if the data stream has reached end-of-file
   * 
   * Returns true if the data stream has reached end-of-file.
   * @return True if the last pushed AVFrame was an EOF marker.
   */
  bool eof()
  {
    // no room or eof
    std::unique_lock<std::mutex> l_rx(m);
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
    std::unique_lock<std::mutex> l_rx(m);

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
    std::unique_lock<std::mutex> l_rx(m);
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
    std::unique_lock<std::mutex> l_rx(m);
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

  void swap(FrameBuffer &o)
  {
    std::unique_lock<std::mutex> l_rx(m);
    AVFrameVideoComponentSink &other = (AVFrameVideoComponentSink &)o;
    std::swap(pixfmt, other.pixfmt);
    std::swap(time_base.num, other.time_Base.num);
    std::swap(time_base.den, other.time_Base.den);
    std::swap(desc, other.desc);
    std::swap(nb_frames, other.nb_frames);
    std::swap(width, other.width);
    std::swap(height, other.height);
    std::swap(time_buf, other.time_buf);
    std::swap(frame_data_sz, other.frame_data_sz);
    std::swap(data_buf, other.data_buf);
    std::swap(wr_time, other.wr_time);
    std::swap(wr_data, other.wr_data);
  }

protected:
  bool readyToPush_threadunsafe() // declared in AVFrameSinkBase
  {
    // no room or eof
    return !has_eof || nb_frames >= (wr_time - time_buf);
  }

  int push_threadunsafe(AVFrame *frame)
  {
    // expects having exclusive access to the user supplied buffer
    if (!nb_frames || full()) // receiving data buffer not set
      return AVERROR(EAGAIN);

    if (frame)
    {
      // format must match
      if (frame->format != pix_fmt)
        return AVERROR(EAGAIN);

      // copy time
      if (frame->pts != AV_NOPTS_VALUE)
        *wr_time = frame->pts;
      else
        *wr_time = NAN;
      ++wr_time;

      for (int i = 0; i < desc->nb_components; ++i)
        copy_component(frame, desc->comp[i], wr_data + i * width * height);
      wr_data += frame_data_sz;
    }
    else // null frame == eof marker
    {
      has_eof = true;
    }
    return 0;
  }

  virtual void reset_threadunsafe(const size_t nframes) // must re-implement to allocate data_buf
  {
    if (pixfmt == AV_PIX_FMT_NONE) // default-constructed unusable empty buffer
    {
      if (nframes > 0)
        throw ffmpegException("This buffer is default-constructed and thus unusable.");
      return;
    }

    // get new buffer size if specified
    // if nframes==0, keep the same capacity
    if (nframes)
      nb_frames = nframes;

    // reallocate only if new buffer size or previous memory was released
    if (nframes || !time_buf)
    {
      // determine the buffer size if not already set
      if (!frame_data_sz)
        frame_data_sz = width * height * desc->nb_components;

      // reallocate
      time_buf = (int64_t *)allocator.allocate(nb_frames * sizeof(int64_t), (uint8_t *)time_buf);
      data_buf = allocator.allocate(nb_frames * frame_data_sz, data_buf);
    }

    // reset the eof flag and write pointer positions
    has_eof = false;
    wr_time = time_buf;
    wr_data = data_buf;
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

  /**
   * Copy one component of a frame
  */
  void copy_component(const AVFrame *frame, const AVComponentDescriptor &d, uint8_t *data)
  {
    int lnsz = frame->linesize[d.plane];
    uint8_t *src = frame->data[d.plane];
    uint8_t *src_end = src + height * lnsz;
    // Copy frame data
    for (; src < src_end; src += lnsz) // for each line
    {
      uint8_t *line = src + d.offset;
      for (int w = 0; w < width; ++w) // for each column
      {
        // get the data
        *(data++) = (*line) >> d.shift;

        // go to next pixel
        line += d.step;
      }
    }
  }

  Allocator allocator;

  AVPixelFormat pixfmt;
  const AVPixFmtDescriptor *desc;

  size_t width;
  size_t height;
  size_t frame_data_sz; // size of each frame in number of bytes

  size_t nb_frames;
  bool has_eof;

  int64_t *time_buf;
  uint8_t *data_buf; // MUST BE SET BY IMPLEMENTATION

  int64_t *wr_time;
  uint8_t *wr_data;
};
}
