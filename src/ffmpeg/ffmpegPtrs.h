#pragma once

#include <memory>
#include <functional>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/frame.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
}

namespace ffmpeg
{
inline void delete_input_ctx(AVFormatContext *ctx)
{
  if (ctx)
    avformat_close_input(&ctx);
}
typedef std::unique_ptr<AVFormatContext, decltype(&delete_input_ctx)> AVInputFormatCtxPtr;
// std::function<void(AVFormatContext *)>

inline void delete_format_ctx(AVFormatContext *ctx) { avformat_free_context(ctx); }
typedef std::unique_ptr<AVFormatContext, decltype(&delete_format_ctx)> AVFormatCtxPtr;

inline void delete_codec_ctx(AVCodecContext *ctx)
{
  if (ctx)
    avcodec_free_context(&ctx);
}
typedef std::unique_ptr<AVCodecContext, decltype(&delete_codec_ctx)> AVCodecCtxPtr;

inline void delete_filter_graph(AVFilterGraph *filter_graph)
{
  if (filter_graph)
    avfilter_graph_free(&filter_graph);
}
typedef std::unique_ptr<AVFilterGraph, decltype(&delete_filter_graph)> AVFilterGraphPtr;

inline void delete_filter_inout(AVFilterInOut *filter_inout)
{
  if (filter_inout)
    avfilter_inout_free(&filter_inout);
}
typedef std::unique_ptr<AVFilterInOut, decltype(&delete_filter_inout)> AVFilterInOutPtr;

inline void delete_av_frame(AVFrame *frame)
{
  if (frame)
    av_frame_free(&frame);
}
typedef std::unique_ptr<AVFrame, decltype(&delete_av_frame)> AVFramePtr;

inline void delete_dict(AVDictionary *dict)
{
  if (dict)
    av_dict_free(&dict);
}
typedef std::unique_ptr<AVDictionary, decltype(&delete_dict)> DictPtr;
}
