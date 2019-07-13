#include <map>
#include <string>
#include <algorithm>

#include <mex.h>

extern "C"
{
#include <libavutil/parseutils.h>
}

#include "ffmpeg/ffmpegException.h"
#include "ffmpeg/mxutils.h"

// function colors = ffmpegcolors()
// function colors = ffmpegcolors("onlynames")
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
    std::map<std::string, const uint8_t *> color_map;
    int index;
    const char *name;
    const uint8_t *rgb;
    for (index = 0; (name = av_get_known_color_name(index, &rgb)); ++index)
    {
        std::string name_str(name);
        std::transform(name_str.begin(), name_str.end(), name_str.begin(), ::tolower); // lower case
        color_map.insert(std::make_pair(std::move(name_str), rgb));
    }

    mxArray *mxInfo;
    if (onlynames)
    {
        // return cellstr
        mxInfo = mxCreateCellMatrix(color_map.size(), 1);
        index = 0;
        for (auto it = color_map.begin(); it != color_map.end(); ++it, ++index)
            mxSetCell(mxInfo, index, mxCreateString(it->first.c_str()));
    }
    else
    { // create struct array
        mxInfo = mxCreateStructMatrix(1, 1, 0, nullptr);

        index = 0;
        for (auto it = color_map.begin(); it != color_map.end(); ++it, ++index)
        {
            mxArray *mxRGB = mxCreateDoubleMatrix(1, 3, mxREAL);
            double *rgb_ptr = mxGetPr(mxRGB);
            rgb_ptr[0] = it->second[0] / 255.0l;
            rgb_ptr[1] = it->second[1] / 255.0l;
            rgb_ptr[2] = it->second[2] / 255.0l;

            const char *fname = it->first.c_str();
            mxAddField(mxInfo, fname);
            mxSetField(mxInfo, 0, fname, mxRGB);
        }
    }

    // if no output requested, display table
    if (nlhs || onlynames)
    {
        plhs[0] = mxInfo;
    }
    else
    {
        mexCallMATLAB(0, nullptr, 1, &mxInfo, "disp");
        mxDestroyArray(mxInfo);
    }
}
