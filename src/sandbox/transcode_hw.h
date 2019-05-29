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

HWDevice *hw_device_get_by_type(AVHWDeviceType type);
HWDevice *hw_device_match_by_codec(const AVCodec *codec);
HWDevice *hw_device_get_by_name(const char *name);
int hw_device_init_from_type(AVHWDeviceType type,
                             const char *device,
                             HWDevice **dev_out);

int hwaccel_decode_init(AVCodecContext *avctx);
int hw_device_setup_for_encode(OutputStream *ost);
