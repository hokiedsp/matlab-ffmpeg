#pragma once

#include "ffmpegAllocator.h"
#include "ffmpegException.h"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/rational.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
}

#include "ffmpegAvRedefine.h"

#include <algorithm>

#define AVERROR_EOB FFERRTAG('E', 'O', 'B', ' ') ///< End of buffer

namespace ffmpeg
{

class FrameBuffer
{
public:
  virtual ~FrameBuffer() {}

  virtual int copy_frame(const AVFrame *frame, const AVRational &time_base) = 0;   // copy frame to buffer
  virtual int read_frame(uint8_t *dst, double *t = NULL, bool advance = true) = 0; // read next frame

  virtual int read_first_frame(uint8_t *dst, double *t = NULL) = 0; // read the last frame in the buffer
  virtual int read_last_frame(uint8_t *dst, double *t = NULL) = 0;  // read the first frame in the buffer

  virtual size_t capacity() const = 0; // number of frames it can hold
  virtual size_t frameSize() const = 0;
  virtual bool readyToWrite() const = 0;
  virtual bool readyToRead() const = 0;
  virtual bool empty() const = 0;       // true if no frame
  virtual bool full() const = 0;        // true if max capacity
  virtual bool eof() const = 0;         // true if no more frame
  virtual bool last() const = 0;        // true if last set of frames (could be empty buffer)
  virtual size_t size() const = 0;      // number of frames written
  virtual size_t available() const = 0; // number of frames remaining to be read
  virtual size_t remaining() const = 0; // remaining available space (in number of frames) to be written

  virtual void reset(const size_t nframes) = 0; // must re-implement to allocate data_buf
  virtual size_t release(uint8_t **data, double **time = NULL) = 0;
  virtual void swap(FrameBuffer &other) = 0;
};

template <class Allocator = ffmpegAllocator<uint8_t>()>
class FrameBufferBase : public FrameBuffer
{
public:
  FrameBufferBase(const size_t w, const size_t h, const AVPixelFormat fmt)
      : pixfmt(fmt), desc(av_pix_fmt_desc_get(fmt)), nb_frames(0), width(w), height(h),
        time_buf(NULL), frame_data_sz(0), data_sz(0), data_buf(NULL),
        has_eof(false), wr_time(NULL), wr_data(NULL), rd_time(NULL), rd_data(NULL)
  {
  }
  FrameBufferBase(const AVFrame *frame) : FrameBufferBase(frame->width, frame->height, (AVPixelFormat)frame->format)
  {
  }

  // default constructor => INVALID object
  FrameBufferBase() : FrameBufferBase(1, 1, AV_PIX_FMT_NONE){};

  // copy constructor
  FrameBufferBase(const FrameBufferBase &other)
      : width(other.width), height(other.height), pixfmt(other.pixfmt), nb_frames(other.nb_frames),
        frame_data_sz(other.frame_data_sz), data_sz(other.data_sz), has_eof(other.has_eof)
  {
    time_buf = (double *)allocator.allocate(nb_frames * sizeof(double));
    std::copy_n(other.time_buf, nb_frames, time_buf);
    wr_time = time_buf + (other.wr_time - other.time_buf);
    rd_time = time_buf + (other.rd_time - other.time_buf);

    data_buf = allocator.allocate(data_sz);
    std::copy_n(other.data_buf, data_sz, data_buf);
    wr_data = data_buf + (other.wr_data - other.data_buf);
    rd_data = data_buf + (other.rd_data - other.data_buf);
  }

  // move constructor
  FrameBufferBase(FrameBufferBase &&other) noexcept
      : width(other.width), height(other.height), pixfmt(other.pixfmt), nb_frames(other.nb_frames),
        time_buf(other.time_buf), frame_data_sz(other.frame_data_sz), data_sz(other.data_sz), data_buf(other.data_buf),
        has_eof(other.has_eof), wr_time(other.wr_time), wr_data(other.wr_data), rd_time(other.rd_time), rd_data(other.rd_data)
  {
    // reset other
    other.pixfmt = AV_PIX_FMT_NONE;
    other.nb_frames = 0;
    other.time_buf = NULL;
    other.data_sz = 0;
    other.data_buf = NULL;
    other.wr_time = NULL;
    other.wr_data = NULL;
    other.rd_time = NULL;
    other.rd_data = NULL;
  }

  // copy assign operator
  FrameBufferBase &operator=(const FrameBufferBase &other)
  {
    width = other.width;
    height = other.height;
    pixfmt = other.pixfmt;
    nb_frames = other.nb_frames;
    frame_data_sz = other.frame_data_sz;
    data_sz = other.data_sz;
    has_eof = other.has_eof;

    time_buf = (double *)allocator.allocate(nb_frames * sizeof(double));
    std::copy_n(other.time_buf, nb_frames, timebuf);
    wr_time = time_buf + (other.wr_time - other.time_buf);
    rd_time = time_buf + (other.rd_time - other.time_buf);

    data_buf = allocator.allocate(data_sz);
    std::copy_n(other.data_buf, data_sz, data_buf);
    wr_data = data_buf + (other.wr_data - other.data_buf);
    rd_data = data_buf + (other.rd_data - other.data_buf);

    return *this;
  }

  // move assign operator
  FrameBufferBase &operator=(FrameBufferBase &&other)
  {
    width = other.width;
    height = other.height;
    pixfmt = other.pixfmt;
    nb_frames = other.nb_frames;
    frame_data_sz = other.frame_data_sz;
    data_sz = other.data_sz;
    has_eof = other.has_eof;

    time_buf = other.time_buf;
    data_sz = other.data_sz;
    data_buf = other.data_buf;
    wr_time = other.wr_time;
    wr_data = other.wr_data;
    rd_time = other.rd_time;
    rd_data = other.rd_data;

    // reset other
    other.pixfmt = AV_PIX_FMT_NONE;
    other.nb_frames = 0;
    other.time_buf = NULL;
    other.data_sz = 0;
    other.data_buf = NULL;
    other.wr_time = NULL;
    other.wr_data = NULL;
    other.rd_time = NULL;
    other.rd_data = NULL;

    return *this;
  }

  virtual ~FrameBufferBase()
  {
    allocator.deallocate((uint8_t *)time_buf, nb_frames * sizeof(double));
    allocator.deallocate(data_buf, data_sz);
  }

  //virtual int copy_frame(const AVFrame *frame, const AVRational &time_base) = 0; // copy frame to buffer
  virtual int read_first_frame(uint8_t *dst, double *t = NULL) // read the last frame in the buffer
  {
    if (wr_time == time_buf)
    {
      if (has_eof) // no data, eof
        return AVERROR_EOF;
      else // no data, empty buffer
        return AVERROR_EOB;
    }

    if (t)
      *t = *time_buf;
    if (dst)
      std::copy_n(data_buf, frame_data_sz, dst);
    return 0;
  }

  virtual int read_last_frame(uint8_t *dst, double *t = NULL) // read the first frame in the buffer
  {
    if (wr_time == time_buf)
    {
      if (has_eof) // no data, eof
        return AVERROR_EOF;
      else // no data, empty buffer
        return AVERROR_EOB;
    }

    if (t)
      *t = *(wr_time - 1);
    if (dst)
      std::copy_n((wr_data - frame_data_sz), frame_data_sz, dst);
    return 0;
  }

  virtual int read_frame(uint8_t *dst, double *t = NULL, bool advance = true) // read next frame
  {
    // if a frame is available, read
    // else if no frame is available & has_eof is set, return EOF
    // else if no frame is available and not eof, return EOB
    // else if writer has not reached the end and reader caught up, return EAGAIN

    if (rd_time < wr_time)
    {
      if (t)
        *t = *rd_time;
      if (dst)
        std::copy_n(rd_data, frame_data_sz, dst);
      if (advance)
      {
        ++rd_time;
        rd_data += frame_data_sz;
      }
      return int(frame_data_sz);
    }
    else if (has_eof)
      return AVERROR_EOF;
    else if (rd_time == time_buf + nb_frames)
      return AVERROR_EOB;
    else
      return AVERROR(EAGAIN);
  }

  virtual size_t capacity() const { return nb_frames; } // number of frames it can hold
  virtual size_t frameSize() const { return frame_data_sz; }
  virtual bool readyToWrite() const { return !full(); }
  virtual bool readyToRead() const { return available() || eof(); }
  virtual bool empty() const { return wr_time == time_buf; }                                  // true if no frame
  virtual bool full() const { return has_eof || wr_time == time_buf + nb_frames; }            // true if max capacity or reached has_eof
  virtual bool eof() const { return has_eof && rd_time == wr_time; };                         // true if last
  virtual bool last() const { return has_eof; };                                              // true if last set of frames (could be empty buffer)
  virtual size_t size() const { return wr_time - time_buf; }                                  // number of frames written
  virtual size_t available() const { return wr_time - rd_time; }                              // number of frames remaining to be read
  virtual size_t remaining() const { return has_eof ? 0 : nb_frames - (wr_time - time_buf); } // remaining available space (in number of frames) to be written

  virtual void reset(const size_t nframes) // must re-implement to allocate data_buf
  {
    if (nframes)
      nb_frames = nframes;

    if (nframes || !time_buf) // reallocate only if the buffer size changes or previous memory was released
      time_buf = (double *)allocator.allocate(nb_frames * sizeof(double), (uint8_t *)time_buf);

    has_eof = false;
    wr_time = time_buf;
    rd_time = time_buf;
  }

  size_t release(uint8_t **data, double **time = NULL)
  {
    size_t rval = wr_time - time_buf; // save the # of frames in releasing buffer
    if (data)
    {
      *data = data_buf;
      data_buf = NULL;
    }
    if (time)
    {
      *time = time_buf;
      time_buf = NULL;
    }

    // reallocate memory for the buffer
    reset(0);

    return rval; // number of frames
  }
  virtual void swap(FrameBuffer &o)
  {
    FrameBufferBase &other = (FrameBufferBase &)o;
    std::swap(pixfmt, other.pixfmt);
    std::swap(desc, other.desc);
    std::swap(nb_frames, other.nb_frames);
    std::swap(width, other.width);
    std::swap(height, other.height);
    std::swap(time_buf, other.time_buf);
    std::swap(data_sz, other.data_sz);
    std::swap(frame_data_sz, other.frame_data_sz);
    std::swap(data_buf, other.data_buf);
    std::swap(wr_time, other.wr_time);
    std::swap(wr_data, other.wr_data);
    std::swap(rd_time, other.rd_time);
    std::swap(rd_data, other.rd_data);
  }

protected:
  Allocator allocator;

  AVPixelFormat pixfmt;
  const AVPixFmtDescriptor *desc;

  size_t nb_frames;
  size_t width;
  size_t height;

  double *time_buf;

  size_t frame_data_sz; // size of each frame in number of bytes
  size_t data_sz;
  uint8_t *data_buf; // MUST BE SET BY IMPLEMENTATION
  bool has_eof;

  double *wr_time;
  uint8_t *wr_data;

  double *rd_time;
  uint8_t *rd_data;
};

template <class Allocator = ffmpegAllocator<uint8_t>>
class ComponentBuffer : public FrameBufferBase<Allocator>
{
public:
  ComponentBuffer() : FrameBufferBase()
  {
  }
  ComponentBuffer(const size_t nframes, const size_t w, const size_t h, const AVPixelFormat fmt)
      : FrameBufferBase(w, h, fmt)
  {
    if (nframes == 0)
      throw ffmpegException("Frame buffer size must be non-zero.");

    // check to see if PixelFormat is compatible
    if (!supportedPixelFormat(fmt))
      throw ffmpegException("Specified AVPixelFormat is not supported by ComponentBuffer.");

    reset(nframes);
  }

  ComponentBuffer(const ComponentBuffer &other)
      : FrameBufferBase(other)
  {
  }

  static bool supportedPixelFormat(const AVPixelFormat fmt)
  {
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    if (!desc || desc->flags & AV_PIX_FMT_FLAG_BITSTREAM) // invalid format
      return false;

    // depths of all components must be single-byte
    bool ok = true;
    for (int i = 0; ok && i < desc->nb_components; ++i)
      ok = desc->comp[i].depth <= 8;

    return ok;
  }

  virtual void reset(const size_t nframes = 0)
  {
    if (pixfmt == AV_PIX_FMT_NONE) // default-constructed unusable empty buffer
    {
      if (nframes > 0)
        throw ffmpegException("This buffer is default-constructed and thus unusable.");
      return;
    }

    FrameBufferBase::reset(nframes);

    if (!frame_data_sz)
      // determine the buffer size
      frame_data_sz = width * height * desc->nb_components;

    // get new buffer size if specified
    if (nframes)
    {
      nb_frames = nframes;
      data_sz = nb_frames * frame_data_sz;
    }

    // allocate data
    if (nframes || !data_buf)
      data_buf = allocator.allocate(data_sz, data_buf);

    // reset read/write pointer positions
    wr_data = data_buf;
    rd_data = data_buf;
  }

  virtual int copy_frame(const AVFrame *frame, const AVRational &time_base)
  {
    // expects having exclusive access to the user supplied buffer
    if (!nb_frames || full()) // receiving data buffer not set
      return AVERROR(EAGAIN);

    if (frame)
    {
      // copy time
      if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
        *wr_time = double(av_rescale_q(frame->best_effort_timestamp, time_base, AV_TIME_BASE_Q) / 100) / (AV_TIME_BASE / 100);
      else
        *wr_time = NAN;
      ++wr_time;

      for (int i = 0; i < desc->nb_components; ++i)
        copy_component(frame, desc->comp[i], wr_data + i * width * height);
      wr_data += frame_data_sz;
    }
    else
    {
      av_log(NULL, AV_LOG_INFO, "ffmpeg::ComponentBuffer::copy_frame()::EOF marked [%d]\n", eof());
      has_eof = true;
    }
    return 0;
  }

private:
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
};

template <class Allocator = ffmpegAllocator<uint8_t>>
class ComponentBufferBDReader : public ComponentBuffer<Allocator>
{
protected:
  bool rd_fwd;

public:
  ComponentBufferBDReader() : ComponentBuffer()
  {
  }
  ComponentBufferBDReader(const size_t nframes, const size_t w, const size_t h, const AVPixelFormat fmt, const bool dir = true)
      : ComponentBuffer(nframes, w, h, fmt), rd_fwd(dir)
  {
    // run reset again if reading backwards (ComponentBuffer already run its version once)
    if (!rd_fwd)
      reset();
  }

  ComponentBufferBDReader(const ComponentBufferBDReader &other)
      : ComponentBuffer(other), rd_fwd(other.rd_fwd)
  {
  }

  // move constructor
  ComponentBufferBDReader(ComponentBufferBDReader &&other) noexcept
      : ComponentBuffer(other), rd_fwd(other.rd_fwd)
  {
  }

  // copy assign operator
  ComponentBufferBDReader &operator=(const ComponentBufferBDReader &other)
  {
    ComponentBuffer::operator=(other);
    rd_fwd = other.rd_fwd;
    return *this;
  }

  // move assign operator
  ComponentBufferBDReader &operator=(ComponentBufferBDReader &&other)
  {
    ComponentBuffer::operator=(other);
    rd_fwd = other.rd_fwd;
    return *this;
  }

  virtual size_t available() const { return (rd_fwd) ? (wr_time - rd_time) : (wr_time >= rd_time) ? (rd_time - time_buf) : 0; } // number of frames remaining to be read
  virtual bool eof() const { return has_eof && ((rd_fwd) ? (rd_time == wr_time) : (wr_time > time_buf && *time_buf == 0.0)); }; // true if next read is the last

  virtual void reset(const size_t nframes = 0)
  {
    ComponentBuffer::reset(nframes);

    // reset read/write pointer positions to the end of the buffer
    if (!rd_fwd)
    {
      rd_time = time_buf + nb_frames;
      rd_data = data_buf + data_sz;
    }
  }

  virtual int copy_frame(const AVFrame *frame, const AVRational &time_base)
  {
    int ret = ComponentBuffer::copy_frame(frame, time_base);

    // if EOF and reading backward, adjust read pointers to the end of buffer
    if (has_eof && !rd_fwd)
    {
      rd_time = wr_time;
      rd_data = wr_data;
    }

    return ret;
  }

  virtual int read_frame(uint8_t *dst, double *t = NULL, bool advance = true) // read next frame
  {
    if (rd_fwd)
      return ComponentBuffer::read_frame(dst, t, advance);

    if (!has_eof && wr_time < time_buf + nb_frames) // forward writer must be done
      return AVERROR(EAGAIN);
    else if (rd_time > 0)
    {
      // move pointers backward by one frame
      --rd_time;
      rd_data -= frame_data_sz;

      if (t)
        *t = *rd_time;
      if (dst)
        std::copy_n(rd_data, frame_data_sz, dst);

      if (!advance)
      {
        ++rd_time;
        rd_data += frame_data_sz;
      }
      return int(frame_data_sz);
    }
    else if (*rd_time == 0)
      return AVERROR_EOF;
    else
      return AVERROR_EOB;
  }
};

// template <typename Allocator = ffmpegAllocator<uint8_t>>
// class PlanarBuffer : public FrameBuffer<Allocator>
// {
//   PlanarBuffer(const AVPixelFormat pixfmt, const size_t nb_frames, uint8_t *buffer);
//   int copy_frame(const AVFrame *frame, const AVRational &time_base); // copy frame to buffer
// };
}
