#pragma once

extern "C" {
#include <libavutil/frame.h>
}

namespace ffmpeg
{
  // Interface Classes
class IAVFrameSink
{
  public:
    virtual bool readyToPush() = 0;
    virtual int push(AVFrame *frame) = 0;

    virtual AVRational getTimeBase() const = 0;
    virtual void setTimeBase(const AVRational &tb) = 0;
};
 
class IAVFrameSource
{
  public:
    virtual bool readyToPop() = 0;
    virtual AVFrame *pop() = 0;
};
}
