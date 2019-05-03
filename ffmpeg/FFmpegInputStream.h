#pragma once

#include <set>
#include <string>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include <mex.h>

struct FFmpegInputStream
{
    AVStream *st;
    AVCodecContext *dec_ctx;
    AVFormatContext *fmt_ctx;

    static const char *field_names[37];

    FFmpegInputStream(AVFormatContext *s, int i, AVDictionary *format_opts = nullptr);
    virtual ~FFmpegInputStream() { avcodec_free_context(&dec_ctx); }

    std::string getMediaType() const
    {
        const char *s = av_get_media_type_string(st->codecpar->codec_type);
        return std::string(s ? s : "unknown");
    }

    static mxArray *createMxInfoStruct(mwSize size);
    void dumpToMatlab(mxArray *mxInfo, mwIndex index) const;
};
