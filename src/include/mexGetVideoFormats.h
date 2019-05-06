#pragma once 
#include <mex.h>

extern "C" {
// #include <libavformat/avformat.h>
// #include <libavfilter/avfiltergraph.h>
// #include <libavcodec/avcodec.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
}

template <class UnaryPredicate>
mxArray *getVideoFormats(UnaryPredicate pred) // formats = getVideoFormats();
{
  mxArray *plhs;

  // build a list of pixel format descriptors
  std::vector<const AVPixFmtDescriptor *> pix_descs;
  pix_descs.reserve(256);
  for (const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_next(NULL);
       pix_desc != NULL;
       pix_desc = av_pix_fmt_desc_next(pix_desc))
  {
    AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(pix_desc);
    if (pred(pix_fmt))
      pix_descs.push_back(pix_desc);
  }

  std::sort(pix_descs.begin(), pix_descs.end(),
            [](const AVPixFmtDescriptor *a, const AVPixFmtDescriptor *b) -> bool { return strcmp(a->name, b->name)<0; });

  const int nfields = 11;
  const char *fieldnames[11] = {
      "Name", "Alias", "NumberOfComponents", "BitsPerPixel",
      "RGB", "Alpha", "Paletted", "HWAccel", "Bayer",
      "Log2ChromaW", "Log2ChromaH"
      };

  plhs = mxCreateStructMatrix(pix_descs.size(), 1, nfields, fieldnames);

  for (int j = 0; j < pix_descs.size(); ++j)
  {
    const AVPixFmtDescriptor *pix_fmt_desc = pix_descs[j];
    AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(pix_fmt_desc);
    mxSetField(plhs, j, "Name", mxCreateString(pix_fmt_desc->name));
    mxSetField(plhs, j, "Alias", mxCreateString(pix_fmt_desc->alias));
    mxSetField(plhs, j, "NumberOfComponents", mxCreateDoubleScalar(pix_fmt_desc->nb_components));
    mxSetField(plhs, j, "Log2ChromaW", mxCreateDoubleScalar(pix_fmt_desc->log2_chroma_w));
    mxSetField(plhs, j, "Log2ChromaH", mxCreateDoubleScalar(pix_fmt_desc->log2_chroma_h));
    mxSetField(plhs, j, "BitsPerPixel", mxCreateDoubleScalar(av_get_bits_per_pixel(pix_fmt_desc)));
    mxSetField(plhs, j, "Paletted", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_PAL) ? "on" : (pix_fmt_desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL) ? "pseudo" : "off"));
    mxSetField(plhs, j, "HWAccel", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_HWACCEL) ? "on" : "off"));
    mxSetField(plhs, j, "RGB", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_RGB) ? "on" : "off"));
    mxSetField(plhs, j, "Alpha", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_ALPHA) ? "on" : "off"));
    mxSetField(plhs, j, "Bayer", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_BAYER) ? "on" : "off"));
  }

  return plhs;
}

inline mxArray *getVideoFormats()
{
  return getVideoFormats([](const AVPixelFormat) { return true; });
}

template <class UnaryPredicate>
mxArray *isSupportedVideoFormat(const mxArray *prhs, UnaryPredicate pred) // tf = isSupportedVideoFormat(name);
{
  // check again the ffmpeg list of pixel format descriptors
  std::string name = mexGetString(prhs);
  AVPixelFormat pix_fmt = av_get_pix_fmt(name.c_str());
  return mxCreateLogicalScalar(pred(pix_fmt));
}

inline mxArray *isSupportedVideoFormat(const mxArray *prhs)
{
  return isSupportedVideoFormat(prhs, [](const AVPixelFormat) { return true; });
}

template <class UnaryPredicate>
AVPixelFormat mexVideoReader::mexArrayToFormat(const mxArray *obj, UnaryPredicate pred)
{
  std::string pix_fmt_str = mexGetString(mxGetProperty(obj, 0, "VideoFormat"));

  // check for special cases
  if (pix_fmt_str == "grayscale")
    return AV_PIX_FMT_GRAY8; //        Y        ,  8bpp

  AVPixelFormat pix_fmt = av_get_pix_fmt(pix_fmt_str.c_str());
  if (pix_fmt == AV_PIX_FMT_NONE) // just in case
    mexErrMsgIdAndTxt("ffmpegVideoReader:InvalidInput", "Pixel format is unknown.");

  if (!pred(pix_fmt))
    mexErrMsgIdAndTxt("ffmpegVideoReader:InvalidInput", "Pixel format is not supported.");

  return pix_fmt;
}

inline AVPixelFormat mexArrayToFormat(const mxArray *prhs)
{
  return mexArrayToFormat(prhs, [](const AVPixelFormat) { return true; });
}
