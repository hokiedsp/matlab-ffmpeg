#pragma once

#include "..\Common\ffmpegAllocator.h"
#include "..\Common\ffmpegException.h"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/rational.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
}

#include "..\Common\ffmpegAvRedefine.h"

#include <algorithm>

#define AVERROR_EOB FFERRTAG('E', 'O', 'B', ' ') ///< End of buffer

namespace ffmpeg
{

class FrameBuffer
{
public:
  virtual ~FrameBuffer() {}

  virtual int copy_frame(const AVFrame *frame, AVRational time_base) = 0; // copy frame to buffer
  virtual int read_frame(uint8_t *dst, double *t = NULL, bool advance = true) = 0; // read next frame
  
  virtual size_t capacity() const = 0;                                                // number of frames it can hold
  virtual size_t frameSize() const = 0;
  virtual bool empty() const = 0;            // true if no frame
  virtual bool full() const = 0;             // true if max capacity
  virtual size_t size() const = 0;              // number of frames written
  virtual size_t available() const = 0;         // number of frames remaining to be read
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
        eof(false), wr_time(NULL), wr_data(NULL), rd_time(NULL), rd_data(NULL)
  {
  }
  FrameBufferBase(const AVFrame *frame) : FrameBufferBase(frame->width, frame->height, (AVPixelFormat)frame->format)
  {
  }

  // default constructor => INVALID object
  FrameBufferBase() : FrameBufferBase(1, 1, AV_PIX_FMT_NONE)
  {
  }; 

  // copy constructor
  FrameBufferBase(const FrameBufferBase &other)
      : width(other.width), height(other.height), pixfmt(other.pixfmt), nb_frames(other.nb_frames),
        frame_data_sz(other.frame_data_sz), data_sz(other.data_sz), eof(other.eof)
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
        eof(other.eof), wr_time(other.wr_time), wr_data(other.wr_data), rd_time(other.rd_time), rd_data(other.rd_data)
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
    eof = other.eof;

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
    eof = other.eof;

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

  //virtual int copy_frame(const AVFrame *frame, AVRational time_base) = 0; // copy frame to buffer

  int read_frame(uint8_t *dst, double *t = NULL, bool advance = true) // read next frame
  {
    if (eof)
      return AVERROR_EOF;
    else if (rd_time == time_buf + nb_frames)
      return AVERROR_EOB;
    else if (rd_time == wr_time)
      return AVERROR(EAGAIN);

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

  size_t capacity() const { return nb_frames; } // number of frames it can hold
  size_t frameSize() const { return frame_data_sz; }
  bool empty() const { return wr_time == time_buf; }           // true if no frame
  bool full() const { return eof || wr_time == time_buf + nb_frames; } // true if max capacity or reached eof
  size_t size() const { return wr_time - time_buf; }              // number of frames written
  size_t available() const { return wr_time - rd_time; } // number of frames remaining to be read

  virtual void reset(const size_t nframes) // must re-implement to allocate data_buf
  {
    if (nframes) 
      nb_frames = nframes;
   
    if (nframes || !time_buf) // reallocate only if the buffer size changes or previous memory was released
      time_buf = (double *)allocator.allocate(nb_frames * sizeof(double), (uint8_t *)time_buf);

    eof = false;
    wr_time = time_buf;
    rd_time = time_buf;
  }

  size_t release(uint8_t **data, double **time = NULL)
  {
    av_log(NULL, AV_LOG_INFO, "releasing frame buffer\n");

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
    FrameBufferBase &other = (FrameBufferBase&)o;
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
  bool eof;

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
  {}
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
    reset(other.nb_frames);
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

  void reset(const size_t nframes = 0)
  {
    if (pixfmt==AV_PIX_FMT_NONE) // default-constructed unusable empty buffer
    {
      if (nframes > 0)
      {
        throw ffmpegException("This buffer is default-constructed and thus unusable.");
      }
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
    {
      av_log(NULL, AV_LOG_INFO, "allocating data buffer [nb_frames=%d,data_sz=%d]\n", nb_frames, data_sz);
      data_buf = allocator.allocate(data_sz, data_buf);
    }

    // reset read/write pointer positions
    wr_data = data_buf;
    rd_data = data_buf;
  }

  int copy_frame(const AVFrame *frame, AVRational time_base)
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
      eof = true;
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

// template <typename Allocator = ffmpegAllocator<uint8_t>>
// class PlanarBuffer : public FrameBuffer<Allocator>
// {
//   PlanarBuffer(const AVPixelFormat pixfmt, const size_t nb_frames, uint8_t *buffer);
//   int copy_frame(const AVFrame *frame, AVRational time_base); // copy frame to buffer
// };
}
