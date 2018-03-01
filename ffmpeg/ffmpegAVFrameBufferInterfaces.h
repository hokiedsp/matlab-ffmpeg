#pragma once

#include "ffmpegMediaStructs.h"

extern "C" {
#include <libavutil/frame.h>
}

#include <chrono>

namespace ffmpeg
{
// Interface Classes
class IAVFrameSink : virtual public IMediaHandler
{
public:
  virtual bool ready() const = 0;
  virtual bool supportedFormat(int format) const = 0;
  virtual void clear(bool deep = false) = 0;
  virtual bool readyToPush() = 0;
  virtual void blockTillReadyToPush() = 0;
  virtual bool blockTillReadyToPush(const std::chrono::milliseconds &rel_time) = 0;
  virtual void push(AVFrame *frame) = 0;
  virtual int push(AVFrame *frame, const std::chrono::milliseconds &rel_time) = 0;
  virtual int tryToPush(AVFrame *frame) = 0;
  virtual bool eof() = 0;
};

class IAVFrameSource : virtual public IMediaHandler
{
public:
  virtual bool ready() const = 0;
  virtual bool supportedFormat(int format) const = 0;
  virtual void clear() = 0;
  virtual bool readyToPop() = 0;
  virtual void blockTillReadyToPop() = 0;
  virtual bool blockTillReadyToPop(const std::chrono::milliseconds &rel_time) = 0;
  virtual void pop(AVFrame *frame) = 0;
  virtual int pop(AVFrame *frame, const std::chrono::milliseconds &rel_time) = 0;
  virtual int tryToPop(AVFrame *frame) = 0;
};
}
