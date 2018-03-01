#pragma once 
#include <mex.h>

extern "C" {
// #include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

template <class UnaryPredicate>
mxArray *getMediaOutputFormats(UnaryPredicate pred)
{
  av_register_all(); // Initialize libavformat and register all the muxers, demuxers and protocols.

  std::vector<AVOutputFormat *> formats;
  formats.reserve(256);
  for (AVOutputFormat *fmt = av_oformat_next(NULL); fmt; fmt = av_oformat_next(fmt))
  {
  // build a list of supported codec descriptors (all the video decoders)
    if (pred(fmt))
      formats.push_back(fmt);
  }

#define NumFileFormatFields 4
  const char *fieldnames[NumFileFormatFields] = {
      "Names", "Description", "Extensions", "MIMETypes"};
  mxArray *rval = mxCreateStructMatrix(formats.size(), 1, NumFileFormatFields, fieldnames);

  for (int index = 0; index < formats.size(); index++)
  {
    mxSetField(rval, index, "Names", mxCreateString(formats[index]->name));
    mxSetField(rval, index, "Description", mxCreateString(formats[index]->long_name));
    mxSetField(rval, index, "Extensions", mxCreateString(formats[index]->extensions));
    mxSetField(rval, index, "MIMETypes", mxCreateString(formats[index]->mime_type));
  }
  return rval;
}

inline mxArray *getAllVideoOutputFormats()
{
  return getMediaOutputFormats([](AVOutputFormat* fmt) { return (fmt->video_codec != AV_CODEC_ID_NONE); });
}

inline mxArray *getAllAudioOutputFormats()
{
  return getMediaOutputFormats([](AVOutputFormat* fmt) { return (fmt->audio_codec != AV_CODEC_ID_NONE); });
}

template <class UnaryPredicate>
mxArray *getMediaInputFormats(UnaryPredicate pred)
{
  av_register_all(); // Initialize libavformat and register all the muxers, demuxers and protocols.

  std::vector<AVInputFormat *> formats;
  formats.reserve(256);
  for (AVInputFormat *fmt = av_iformat_next(NULL); fmt; fmt = av_iformat_next(fmt))
  {
  // build a list of supported codec descriptors (all the video decoders)
    if (pred(fmt))
      formats.push_back(fmt);
  }

#define NumFileFormatFields 4
  const char *fieldnames[NumFileFormatFields] = {
      "Names", "Description", "Extensions", "MIMETypes"};
  mxArray *rval = mxCreateStructMatrix(ifmtptrs.size(), 1, NumFileFormatFields, fieldnames);

  for (int index = 0; index < formats.size(); index++)
  {
    mxSetField(rval, index, "Names", mxCreateString(formats[index]->name));
    mxSetField(rval, index, "Description", mxCreateString(formats[index]->long_name));
    mxSetField(rval, index, "Extensions", mxCreateString(formats[index]->extensions));
    mxSetField(rval, index, "MIMETypes", mxCreateString(formats[index]->mime_type));
  }
  return rval;
}

inline mxArray *getMediaCompressions()
{
  return getMediaCompressions([](const AVCodecDescriptor* desc) { return !strstr(desc->name, "_deprecated"); });
}
