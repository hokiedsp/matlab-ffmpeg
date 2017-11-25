extern "C" {
#include <libavutil/rational.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
}

namespace ffmpeg
{

class FrameBuffer
{
public:
  FrameBuffer(const AVPixelFormat pixfmt, const size_t nb_frames, uint8_t *buffer) : sz(nb_frames), buf(buffer)
  {
    if (buf) sz = 0;
  }
  virtual int size() const;                                               // number of frames it can hold
  virtual int copy_frame(const AVFrame *frame, AVRational time_base) = 0; // copy frame to buffer
protected:
  size_t sz;
  uint8_t *frame_buf;
  double *time_buf;
  AVPixelFormat pixfmt;
};

class ComponentBuffer : public FrameBuffer
{
  ComponentBuffer(const AVPixelFormat pixfmt, const size_t nb_frames, uint8_t *buffer);
  int size() const; // number of frames it can hold
  int copy_frame(const AVFrame *frame, AVRational time_base); // copy frame to buffer
};

class PlanarBuffer : public FrameBuffer
{
  PlanarBuffer(const AVPixelFormat pixfmt, const size_t nb_frames, uint8_t *buffer);
  int size() const; // number of frames it can hold
  int copy_frame(const AVFrame *frame, AVRational time_base); // copy frame to buffer
};

}
