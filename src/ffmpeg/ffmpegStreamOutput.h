#pragma once

#include "ffmpegStream.h"
// #include "ffmpegAvRedefine.h"
#include "ffmpegAVFrameBufferInterfaces.h"
#include "ffmpegAVFrameEndpointInterfaces.h"
#include "ffmpegMediaStructs.h"

extern "C"
{
    // #include <libavformat/avformat.h>
    // #include <libavcodec/avcodec.h>
    // #include <libavutil/pixdesc.h>
}

namespace ffmpeg
{

/**
 * \brief Class to manage AVStream
 */
class OutputStream : virtual public BaseStream, public IAVFrameSink
{
public:
    OutputStream(IAVFrameSourceBuffer *buf);
    virtual ~OutputStream();

    virtual bool ready() { return ctx && src; }

    virtual AVStream *open();
    virtual void close() {}

    IAVFrameSourceBuffer &getSourceBuffer() const
    {
        if (src) return *src;
        throw ffmpegException("No buffer.");
    }
    void setSourceBuffer(IAVFrameSourceBuffer &buf)
    {
        if (src) src->clrDst();
        src = &buf;
        src->setDst(*this);
    }
    void clrSourceBuffer()
    {
        if (src)
        {
            src->clrDst();
            src = NULL;
        }
    }

    virtual int reset()
    {
        avcodec_flush_buffers(ctx);
        return 0;
    } // reset decoder states
    virtual int OutputStream::processFrame(AVPacket *packet);

protected:
    IAVFrameSourceBuffer *src;
    AVDictionary *encoder_opts;
};

typedef std::vector<OutputStream *> OutputStreamPtrs;

class OutputVideoStream : public VideoStream, public OutputStream
{
public:
    OutputVideoStream(IAVFrameSourceBuffer *buf = NULL);
    virtual ~OutputVideoStream();

    AVPixelFormats getPixelFormats() const;
    AVPixelFormat choose_pixel_fmt(AVPixelFormat target) const;
    AVPixelFormats choose_pix_fmts() const;

private:
    bool keep_pix_fmt;
};

class OutputAudioStream : public AudioStream, public OutputStream
{
public:
    OutputAudioStream(IAVFrameSourceBuffer *buf = NULL) : OutputStream(buf) {}
    virtual ~OutputAudioStream() {}

private:
};
} // namespace ffmpeg
