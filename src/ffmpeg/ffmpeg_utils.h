#pragma once

#include <memory>
#include <string>
#include <vector>

#include <mex.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
}
#define number_of_elements_in_array(myarray) (sizeof(myarray) / sizeof(myarray[0]))

inline void av_dict_delete(AVDictionary *dict)
{
    if (dict) av_dict_free(&dict);
}
#define AVDictionaryAutoDelete(dict) std::unique_ptr<AVDictionary, decltype(&av_dict_delete)> cleanup_##dict(dict, &av_dict_delete)

inline void av_codec_context_delete(AVCodecContext *ctx)
{
    if (ctx) avcodec_free_context(&ctx);
}
#define AVCodecContextAutoDelete(ctx) std::unique_ptr<AVCodecContext, decltype(&av_codec_context_delete)> cleanup_##dict(ctx, &av_codec_context_delete)
#define AVCodecContextUniquePtr std::unique_ptr<AVCodecContext, decltype(&av_codec_context_delete)>
#define NewAVCodecContextUniquePtr(ctx) std::unique_ptr<AVCodecContext, decltype(&av_codec_context_delete)>(ctx, &av_codec_context_delete)

/**
 * Check if the stream st contained in s is matched by the stream specifier
 * spec.
 *
 * See the "stream specifiers" chapter in the documentation for the syntax
 * of spec.
 *
 * @return  >0 if st is matched by spec;
 *          0  if st is not matched by spec;
 *          AVERROR code if spec is invalid
 * 
 * @note Unlike libformat's avformat_match_stream_specifier(), this function
 *       logs AV_LOG_ERROR if matching fails (i.e., returns <0).
 *
 * @note  A stream specifier can match several streams in the format.
 */
int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec);

/*
 * filter_codec_opts   
 * 
 * @param[in] opts      Points to an AVDictionary containing arbitrary content
 * @param[in] codec_id  Code ID enum. If stream already has an assigned codec, 
 *                      use st->codecpar->codec_id
 * @param[in] s         Pointer to a Format Context
 * @param[in] st        Pointer to a Stream in s
 * @param[in] codec     Pointer to a codec to use. For a decoder (or an encoder 
 *                      with an already assigned codec), pass a nullptr.
 * 
 * @return  Pointer to a newly allocated AVDictionary containing the selected 
 *          key-value pairs from the given dictionary opts. Caller is 
 *          responsible to free the dictionary.
 * 
 * @throws  if a stream specifier is invalid in any dictionary entry
 */
AVDictionary *filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id,
                                AVFormatContext *s, AVStream *st,
                                AVCodec *codec = nullptr);
