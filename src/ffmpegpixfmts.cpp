#include <map>
#include <string>
#include <algorithm>

#include <mex.h>

extern "C"
{
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <ffmpegException.h>
#include "utils/mxutils.h"

// function colors = ffmpegpixfmts()
// function colors = ffmpegpixfmts("onlynames")
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nlhs > 1 || nrhs > 1)
        mexErrMsgTxt("Takes no input argument and produces 1 output.");

    // parse the input arguments if given
    std::vector<std::string> options;
    bool onlynames = false;
    if (nrhs > 0)
    {
        options = mxParseStringArgs(nrhs, prhs, 1, true);
        auto it = std::find(std::begin(options), std::end(options), "onlynames");
        if (it != options.end())
            onlynames = true;
        if (options.size() > (size_t)onlynames)
            mexErrMsgTxt("Only input argument supported is \"onlynames\".");
    }

    // initialize ffmpeg::Exception to capture fatal error in ffmpeg
    ffmpeg::Exception::initialize();

    // gather color data
    std::map<std::string, const AVPixFmtDescriptor *> pixfmt_map;
    for (const AVPixFmtDescriptor *desc = NULL; (desc = av_pix_fmt_desc_next(desc));)
        pixfmt_map.insert(std::make_pair(desc->name, desc));

    mxArray *mxInfo;
    if (onlynames)
    {
        // return cellstr
        mxInfo = mxCreateCellMatrix(pixfmt_map.size(), 1);
        int index = 0;
        for (auto it = pixfmt_map.begin(); it != pixfmt_map.end(); ++it, ++index)
            mxSetCell(mxInfo, index, mxCreateString(it->first.c_str()));
        plhs[0] = mxInfo;
    }
    else
    {
        // create struct array
        const char *fnames[] = {"name", "input", "output", "hwaccel", "paletted", "bitstream", "nb_components", "bits_per_pixel"};
        mxInfo = mxCreateStructMatrix(pixfmt_map.size(), 1, 8, fnames);

        int index = 0;
        for (auto it = pixfmt_map.begin(); it != pixfmt_map.end(); ++it, ++index)
        {
            auto pix_desc = it->second;
            auto pix_fmt = av_pix_fmt_desc_get_id(pix_desc);
            mxSetField(mxInfo, index, "name", mxCreateString(pix_desc->name));
            auto test = mxCreateLogicalScalar(sws_isSupportedInput(pix_fmt));
            mxSetField(mxInfo, index, "input", mxCreateLogicalScalar(sws_isSupportedInput(pix_fmt)));
            mxSetField(mxInfo, index, "output", mxCreateLogicalScalar(sws_isSupportedOutput(pix_fmt)));
            mxSetField(mxInfo, index, "hwaccel", mxCreateLogicalScalar(pix_desc->flags & AV_PIX_FMT_FLAG_HWACCEL));
            mxSetField(mxInfo, index, "paletted", mxCreateLogicalScalar(pix_desc->flags & AV_PIX_FMT_FLAG_PAL));
            mxSetField(mxInfo, index, "bitstream", mxCreateLogicalScalar(pix_desc->flags & AV_PIX_FMT_FLAG_BITSTREAM));
            mxSetField(mxInfo, index, "nb_components", mxCreateDoubleScalar(pix_desc->nb_components));
            mxSetField(mxInfo, index, "bits_per_pixel", mxCreateDoubleScalar(av_get_bits_per_pixel(pix_desc)));
        }

        // convert it to table
        mxArray *mxTable;
        mexCallMATLAB(1, &mxTable, 1, &mxInfo, "struct2table");
        plhs[0] = mxTable;
    }
}
