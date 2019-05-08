#pragma once

extern "C"
{
// #include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
// #include <libavutil/fifo.h>
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext.h"
#include "libavutil/buffer.h"
}

#include "transcode_inputstream.h"
#include "transcode_outputstream.h"

enum HWAccelID
{
    HWACCEL_NONE = 0,
    HWACCEL_AUTO,
    HWACCEL_GENERIC,
    HWACCEL_VIDEOTOOLBOX,
    HWACCEL_QSV,
    HWACCEL_CUVID,
};

struct HWAccel
{
    const char *name;
    int (*init)(AVCodecContext *s);
    enum HWAccelID id;
    enum AVPixelFormat pix_fmt;
};

struct HWDevice
{
    char *name;
    enum AVHWDeviceType type;
    AVBufferRef *device_ref;
};

int hwaccel_decode_init(AVCodecContext *avctx);
int hw_device_setup_for_decode(InputStream *ist);
int hw_device_setup_for_encode(OutputStream *ost);
