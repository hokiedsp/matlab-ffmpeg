#pragma once

#include "ffmpegAVFrameBufferBases.h"

extern "C" {
#include <libavutil/frame.h>
}

#include <chrono>

namespace ffmpeg
{
/**
 * \brief   Template AVFrame sink to manage receive & send buffers
 */
template <typename Buffer>
class AVFrameDoubleBufferSink : public AVFrameSinkBase
{
protected:
  /**
 * \brief Constructor
 * 
 * Passes all the input parameters to the internal buffers.
 */
  template <class... Args>
  AVFrameDoubleBufferSink(Args... args)
  {
    // create 2 buffers
    buffers.emplace_back(args...);
    buffers.emplace_back(args...);

    // copy the buffer media type
    type = buffers[0].getMediaType();

    // initilaize receiver & sender
    receiver = buffers.begin();
    sender = buffers.end(); // data not available
  };

  /**
   * \brief Copy constructor
   */
  AVFrameDoubleBufferSink(const AVFrameDoubleBufferSink &src) : AVFrameSinkBase(src)
  {
    buffers.emplace_back(src.buffers[0]);
    buffers.emplace_back(src.buffers[1]);
    if (src.receiver == src.buffers.end())
      receiver = buffers.end();
    else
      receiver = buffers.begin() + (src.receiver != src.buffers.begin());

    if (src.sender == src.buffers.end())
      sender = buffers.end();
    else
      sender = buffers.begin() + (src.sender != src.buffers.begin());
  }

  /**
   * \brief Move constructor
   */
  AVFrameDoubleBufferSink(AVFrameDoubleBufferSink &&src) : AVFrameSinkBase(src)
  {
    buffers = std::move(src.buffers);
    receiver = std::move(src.receiver);
    sender = std::move(src.sender);
  }

public:
  virtual ~AVFrameDoubleBufferSink(){};

  /**
   * \brief Perform a task on sender buffer
   * 
   * \param[in] op Function to process the sender buffer. Must have the signature
   *            \code{c++}
   *            bool op(Buffer &sender);
   *            \endcode
   *            The function shall return true if the sender buffer can be released
   *
   * \note Supports simultaneous accesses from multiple threads with shared_mutex
   * 
   */
  template<ReadOp>
  bool processSenderBuffer(ReadOp op)
  {
    if (sender==buffers.end())

    return *sender;
  }

  /**
   * \brief Perform the same operation on both buffers
   * 
   * \note Thread unsafe
   */
  template <typename BufferOp>
  void forEachBuffer(BufferOp op)
  {
    op(buffers[0]);
    op(buffers[1]);
  }

  /**
   * \brief Perform the same const operation on both buffers
   * 
   * \note Thread unsafe
   */
  template <typename BufferConstOp>
  void forEachBufferConst(BufferOp op) const
  {
    op(buffers[0]);
    op(buffers[1]);
  }

  /**
   * \brief True if given FFmpeg format is valid
   */
  bool supportedFormat(int format) const
  {
    return buffers[0].supportedFormat(format);
  }

protected:
  virtual bool readyToPush_threadunsafe() const
  {
    return receiver.readyToPush();
  }
  virtual int push_threadunsafe(AVFrame *frame)
  {
    // receive the frame
    int rval = receiver.push(frame);

    // if receiver is full, swap

  }
  virtual bool clear_threadunsafe(bool deep = false)
  {
    bool rval = buffers[0].clear(deep);
    rval = buffers[1].clear(deep);
  }

  virtual bool eof_threadunsafe() const
  {
    bool sender.eof();
  }

private:
  typedef std::vector<Buffer> Buffers;
  Buffers buffers;
  Buffers::iterator receiver; // receives new AVFrame
  Buffers::iterator sender;    // sends new stored AVFrame data
};
