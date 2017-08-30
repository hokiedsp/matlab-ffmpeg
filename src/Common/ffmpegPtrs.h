#pragma once

#include <memory>
#include <functional>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/frame.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace ffmpeg
{
typedef std::unique_ptr<AVDictionary, std::function<void(AVDictionary *)>> DictPtr;
inline void delete_dict(AVDictionary *dict) { if (dict) av_dict_free(&dict); }

typedef std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext *)>> CodecCtxPtr;
inline void delete_codec_ctx(AVCodecContext *ctx) { if (ctx) avcodec_free_context(&ctx); }

typedef std::unique_ptr<AVFormatContext, std::function<void(AVFormatContext *)>> FormatCtxPtr;
inline void delete_input_ctx(AVFormatContext *ctx) { if (ctx) avformat_close_input(&ctx); }
inline void delete_format_ctx(AVFormatContext *ctx) { avformat_free_context(ctx); }
}
