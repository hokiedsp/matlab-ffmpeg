#pragma once

#include "AVFrameBufferBases.h"

#include <queue>

namespace ffmpeg
{

// AVFrame queue
class AVFrameQueue : virtual public AVFrameSinkBase, virtual public AVFrameSourceBase
{
public:
  AVFrameQueue(AVMediaType media, size_t N = 2) : type(media), max_size(N) {} // queue size
  virtual ~AVFrameQueue() {}
  bool supportedFormat(int format) const { return true; } // accepts all formats

protected:
  bool readyToPush_threadunsafe() // declared in AVFrameSinkBase
  {
    return Q.size() < max_size;
  }
  bool readyToPop_threadunsafe() // declared in AVFrameSourceBase
  {
    return Q.size();
  }
  void push_threadunsafe(AVFrame *frame) // declared in AVFrameSinkBase
  {
    // guaranteed readyToPush() returns true
    Q.push(frame);

    // notify the source-end for the arrival of new data
    cv_tx.notify();
  }
  void pop_threadunsafe(AVFrame *frame) // declared in AVFrameSourceBase
  {
    // guaranteed readyToPop() returns true
    AVFrame *rval = Q.front();
    Q.pop();

    av_frame_unref(frame);
    av_frame_move_ref(frame, rval);
    av_frame_free(rval);
    // notify the sink-end for the arrival of new data
    cv_rx.notify();
  }

private:
  AVMediaType type;
  size_t max_size;
  std::queue<AVFrame *> Q; // queue containing
};
}
