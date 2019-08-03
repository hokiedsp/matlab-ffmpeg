#pragma once

extern "C"
{
#include <libavutil/frame.h>
}

#include "ffmpegBase.h"
#include "ffmpegMediaStructs.h"

#include <chrono>

namespace ffmpeg
{

class IAVFrameSource;
class IAVFrameSink;

// Interface Classes
class IAVFrameSinkBuffer
{
  public:
  virtual bool ready() const = 0;
  virtual void kill() = 0;

  virtual IAVFrameSource &getSrc() const = 0;
  virtual void setSrc(IAVFrameSource &src) = 0;
  virtual void clrSrc() = 0;

  virtual void clear() = 0;
  virtual size_t size() noexcept = 0;
  virtual bool empty() noexcept = 0;
  virtual bool full() noexcept = 0;
  virtual bool hasEof() noexcept = 0; // true if buffer contains EOF

  virtual bool linkable() const = 0;
  virtual void follow(IAVFrameSinkBuffer &master) = 0;
  virtual void lead(IAVFrameSinkBuffer &slave) = 0;

  virtual bool readyToPush() = 0;
  virtual void blockTillReadyToPush() = 0;
  virtual bool
  blockTillReadyToPush(const std::chrono::milliseconds &timeout_duration) = 0;
  virtual void push(AVFrame *frame) = 0;
  virtual bool push(AVFrame *frame,
                    const std::chrono::milliseconds &timeout_duration) = 0;
  virtual bool tryToPush(AVFrame *frame) = 0;

  virtual AVFrame *peekToPush() = 0;
  virtual void push() = 0;
};

class IAVFrameSourceBuffer
{
  public:
  virtual bool ready() const = 0;
  virtual void kill() = 0;

  virtual IAVFrameSink &getDst() const = 0;
  virtual void setDst(IAVFrameSink &dst) = 0;
  virtual void clrDst() = 0;

  virtual const MediaParams &getMediaParams() const = 0;

  virtual void clear() = 0;
  virtual size_t size() noexcept = 0;
  virtual bool empty() noexcept = 0;
  virtual bool full() noexcept = 0;

  virtual bool readyToPop() = 0;
  virtual void blockTillReadyToPop() = 0;
  virtual bool
  blockTillReadyToPop(const std::chrono::milliseconds &timeout_duration) = 0;
  virtual AVFrame *peekToPop() = 0;
  virtual void pop() = 0;
  virtual void pop(AVFrame *frame, bool *eof = nullptr) = 0;
  virtual bool pop(AVFrame *frame, bool *eof,
                   const std::chrono::milliseconds &timeout_duration) = 0;
  virtual bool tryToPop(AVFrame *frame, bool *eof = nullptr) = 0;

  virtual bool eof() = 0;
};

class IAVFrameBuffer : public IAVFrameSourceBuffer, public IAVFrameSinkBuffer
{
};
} // namespace ffmpeg
