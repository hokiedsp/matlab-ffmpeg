#pragma once

#include "ffmpegAVFrameBufferInterfaces.h"

namespace ffmpeg
{
// interface classes for AVFrame endpoints such as stream decoder/encoder and sink & source filters

///
//  Interface for source endpoints: input stream (decoder) and source filter. The endpoint
//  produces AVFrame, which gets pushed to the assigned sink buffer
class IAVFrameSource : virtual public IMediaHandler
{
  public:
  virtual IAVFrameSinkBuffer &getSinkBuffer() const = 0;
  virtual void setSinkBuffer(IAVFrameSinkBuffer &new_buf) = 0;
  virtual void clrSinkBuffer() = 0;
};

/* 
// Inteface for sink endpoints: output stream (encoder) and sink filter. The 
// endpoint pulls AVFrame from the assigned source buffer
*/
class IAVFrameSink : virtual public IMediaHandler
{
  public:
  virtual IAVFrameSourceBuffer &getSourceBuffer() const = 0;
  virtual void setSourceBuffer(IAVFrameSourceBuffer &new_buf) = 0;
  virtual void clrSourceBuffer() = 0;
};
} // namespace ffmpeg
