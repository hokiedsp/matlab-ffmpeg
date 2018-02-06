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
void getVideoFormats(int nlhs, mxArray *plhs[], UnaryPredicate pred) // formats = getVideoFormats();
{
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

  plhs[0] = mxCreateStructMatrix(pix_descs.size(), 1, nfields, fieldnames);

  for (int j = 0; j < pix_descs.size(); ++j)
  {
    const AVPixFmtDescriptor *pix_fmt_desc = pix_descs[j];
    AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(pix_fmt_desc);
    mxSetField(plhs[0], j, "Name", mxCreateString(pix_fmt_desc->name));
    mxSetField(plhs[0], j, "Alias", mxCreateString(pix_fmt_desc->alias));
    mxSetField(plhs[0], j, "NumberOfComponents", mxCreateDoubleScalar(pix_fmt_desc->nb_components));
    mxSetField(plhs[0], j, "Log2ChromaW", mxCreateDoubleScalar(pix_fmt_desc->log2_chroma_w));
    mxSetField(plhs[0], j, "Log2ChromaH", mxCreateDoubleScalar(pix_fmt_desc->log2_chroma_h));
    mxSetField(plhs[0], j, "BitsPerPixel", mxCreateDoubleScalar(av_get_bits_per_pixel(pix_fmt_desc)));
    mxSetField(plhs[0], j, "Paletted", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_PAL) ? "on" : (pix_fmt_desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL) ? "pseudo" : "off"));
    mxSetField(plhs[0], j, "HWAccel", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_HWACCEL) ? "on" : "off"));
    mxSetField(plhs[0], j, "RGB", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_RGB) ? "on" : "off"));
    mxSetField(plhs[0], j, "Alpha", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_ALPHA) ? "on" : "off"));
    mxSetField(plhs[0], j, "Bayer", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_BAYER) ? "on" : "off"));
  }
}

inline void getVideoFormats(int nlhs, mxArray *plhs[])
{
  getVideoFormats(nlhs, plhs, [](const AVPixelFormat) { return true; });
}
