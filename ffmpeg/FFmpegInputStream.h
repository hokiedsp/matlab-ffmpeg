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

    FFmpegInputStream() : st(nullptr), dec_ctx(nullptr), fmt_ctx(nullptr) {} // default xtor
    FFmpegInputStream(const FFmpegInputStream &) = delete; // non construction-copyable
    FFmpegInputStream(FFmpegInputStream &&src) : st(src.st), dec_ctx(src.dec_ctx), fmt_ctx(src.fmt_ctx) // move xtor
    {
        src.st = nullptr;
        src.dec_ctx = nullptr;
        src.fmt_ctx = nullptr;
    }

    FFmpegInputStream &operator=(const FFmpegInputStream &) = delete; // non copyable
    FFmpegInputStream &operator=(FFmpegInputStream && src) = delete; // non movable

    FFmpegInputStream(AVFormatContext *s, int i, AVDictionary *format_opts = nullptr);
    virtual ~FFmpegInputStream()
    {
        if (dec_ctx)
            avcodec_free_context(&dec_ctx);
    }

    std::string getMediaType() const
    {
        const char *s = av_get_media_type_string(st->codecpar->codec_type);
        return std::string(s ? s : "unknown");
    }

    static mxArray *createMxInfoStruct(mwSize size);
    void dumpToMatlab(mxArray *mxInfo, mwIndex index) const;
};
