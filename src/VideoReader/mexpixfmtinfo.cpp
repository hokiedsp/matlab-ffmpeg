#include "../Common/mexClassHandler.h"
#include <mex.h>
extern "C" {
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <vector>
#include <string>
#include <stdexcept>

mxArray *buildPixFmtDescStruct(std::vector<const AVPixFmtDescriptor *> &pix_descs)
{
  const int nfields = 17;
  const char *fieldnames[17] = {
      "Name", "Components", "Log2ChromaW", "Log2ChromaH",
      "SupportSwsInput","SupportSwsOutput",
      "Endianness", "Palletted", "Bitstream", "HWAccel",
      "Planar", "RGB", "Alpha", "Bayer",
      "Alias", "BitsPerPixel", "PaddedBitsPerPixel"};
  const int comp_nfields = 5;
  const char *comp_fieldnames[5] = {"Plane", "Step", "Offset", "Shift", "Depth"};

  mxArray *S = mxCreateStructMatrix(pix_descs.size(), 1, nfields, fieldnames);

  for (int j = 0; j < pix_descs.size(); ++j)
  {
    const AVPixFmtDescriptor *pix_fmt_desc = pix_descs[j];
    AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(pix_fmt_desc);
    bool is_bitstream = (pix_fmt_desc->flags & AV_PIX_FMT_FLAG_BITSTREAM);
    mxSetField(S, j, "Name", mxCreateString(pix_fmt_desc->name));
    mxSetField(S, j, "Alias", mxCreateString(pix_fmt_desc->alias));
    mxSetField(S, j, "Log2ChromaW", mxCreateDoubleScalar(pix_fmt_desc->log2_chroma_w));
    mxSetField(S, j, "Log2ChromaH", mxCreateDoubleScalar(pix_fmt_desc->log2_chroma_h));
    mxSetField(S, j, "BitsPerPixel", mxCreateDoubleScalar(av_get_bits_per_pixel(pix_fmt_desc)));
    mxSetField(S, j, "PaddedBitsPerPixel", mxCreateDoubleScalar(av_get_padded_bits_per_pixel(pix_fmt_desc)));
    mxSetField(S, j, "SupportSwsInput", mxCreateString(sws_isSupportedInput(pix_fmt) ? "on" : "off"));
    mxSetField(S, j, "SupportSwsOutput", mxCreateString(sws_isSupportedOutput(pix_fmt) ? "on" : "off"));
    mxSetField(S, j, "Endianness", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_BE) ? "big" : "little"));
    mxSetField(S, j, "Palletted", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_PAL) ? "on" : (pix_fmt_desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL) ? "pseudo" : "off"));
    mxSetField(S, j, "Bitstream", mxCreateString(is_bitstream ? "on" : "off"));
    mxSetField(S, j, "HWAccel", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_HWACCEL) ? "on" : "off"));
    mxSetField(S, j, "Planar", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_PLANAR) ? "on" : "off"));
    mxSetField(S, j, "RGB", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_RGB) ? "on" : "off"));
    mxSetField(S, j, "Alpha", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_ALPHA) ? "on" : "off"));
    mxSetField(S, j, "Bayer", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_BAYER) ? "on" : "off"));

    mxArray *comps = mxCreateStructMatrix(pix_fmt_desc->nb_components, 1, comp_nfields, comp_fieldnames);
    mxSetField(S, j, "Components", comps);
    for (int i = 0; i < pix_fmt_desc->nb_components; ++i)
    {
      mxSetField(comps, i, "Plane", mxCreateDoubleScalar(1 + pix_fmt_desc->comp[i].plane));
      mxSetField(comps, i, "Step", mxCreateDoubleScalar(is_bitstream ? pix_fmt_desc->comp[i].step : (pix_fmt_desc->comp[i].step * 8)));
      mxSetField(comps, i, "Offset", mxCreateDoubleScalar(is_bitstream ? pix_fmt_desc->comp[i].offset : (pix_fmt_desc->comp[i].offset * 8)));
      mxSetField(comps, i, "Shift", mxCreateDoubleScalar(pix_fmt_desc->comp[i].shift));
      mxSetField(comps, i, "Depth", mxCreateDoubleScalar(pix_fmt_desc->comp[i].depth));
    }
  }
  return S;
}

// pixfmt = ffmpegpixfmtinfo(name)
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  std::vector<const AVPixFmtDescriptor *> pix_descs(1);
  if (nrhs > 0) // pixel format name given
  {
    std::string name;
    try
    {
      if (nrhs != 1)
        throw std::invalid_argument("");
      name = mexGetString(prhs[0]);
    }
    catch (...)
    {
      mexErrMsgTxt("Must input pixel format name.");
    }
    AVPixelFormat pix_fmt = av_get_pix_fmt(name.c_str());
    if (pix_fmt == AV_PIX_FMT_NONE)
      mexErrMsgTxt("Invalid pixel format name given.");
    
    pix_descs[0] = av_pix_fmt_desc_get(pix_fmt);
  }
  else // list them all
  {
    int nb_pixfmts = 0;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_next(NULL);
    for (; pix_desc != NULL; pix_desc = av_pix_fmt_desc_next(pix_desc), ++nb_pixfmts);
    pix_descs.resize(nb_pixfmts);
    pix_desc = NULL;
    for (auto it = pix_descs.begin(); it < pix_descs.end(); ++it)
      *it = pix_desc = av_pix_fmt_desc_next(pix_desc);
  }

  plhs[0] = buildPixFmtDescStruct(pix_descs);
}
