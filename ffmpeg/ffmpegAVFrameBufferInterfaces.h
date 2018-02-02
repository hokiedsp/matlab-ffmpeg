#pragma once

extern "C" {
#include <libavutil/frame.h>
}

#include <chrono>

namespace ffmpeg
{
  // Interface Classes
class IAVFrameSink
{
  public:
    virtual bool readyToPush() = 0;
    virtual int push(AVFrame *frame) = 0;
    virtual int push(AVFrame *frame, const std::chrono::milliseconds &rel_time) = 0;

    virtual AVMediaType getMediaType() const = 0;
    virtual AVRational getTimeBase() const = 0;
    virtual void setTimeBase(const AVRational &tb) = 0;
};
 
class IAVFrameSource
{
  public:
    virtual bool readyToPop() = 0;
    virtual int pop(AVFrame *&frame) = 0;
    virtual int pop(AVFrame *&frame, const std::chrono::milliseconds &rel_time) = 0;
};
}
