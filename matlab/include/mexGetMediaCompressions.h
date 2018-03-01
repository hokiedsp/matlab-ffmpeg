#pragma once 
#include <mex.h>

extern "C" {
// #include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

template <class UnaryPredicate>
mxArray *getMediaCompressions(UnaryPredicate pred) // formats = getVideoFormats();
{
  // make sure all the codecs are loaded
  avcodec_register_all();

  // build a list of supported codec descriptors (all the video decoders)
  std::vector<const AVCodecDescriptor *> codecs;
  codecs.reserve(256);
  for (const AVCodecDescriptor *desc = avcodec_descriptor_next(NULL);
       desc;
       desc = avcodec_descriptor_next(desc))
  {
    if (pred(desc))
      codecs.push_back(desc);
  }

  std::sort(codecs.begin(), codecs.end(),
            [](const AVCodecDescriptor *a, const AVCodecDescriptor *b) -> bool { return strcmp(a->name, b->name) < 0; });

  const int nfields = 4;
  const char *fieldnames[4] = {
      "Name", "Lossless", "Lossy", "Description"};

  plhs[0] = mxCreateStructMatrix(codecs.size(), 1, nfields, fieldnames);

  for (int j = 0; j < codecs.size(); ++j)
  {
    const AVCodecDescriptor *desc = codecs[j];
    mxSetField(plhs[0], j, "Name", mxCreateString(desc->name));
    mxSetField(plhs[0], j, "Lossless", mxCreateString((desc->props & AV_CODEC_PROP_LOSSLESS) ? "on" : "off"));
    mxSetField(plhs[0], j, "Lossy", mxCreateString((desc->props & AV_CODEC_PROP_LOSSY) ? "on" : "off"));
    mxSetField(plhs[0], j, "Description", mxCreateString(desc->long_name ? desc->long_name : ""));
  }
}

inline mxArray *getMediaCompressions()
{
  return getMediaCompressions([](const AVCodecDescriptor* desc) { return !strstr(desc->name, "_deprecated"); });
}
