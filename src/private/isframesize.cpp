#include <memory>
#include <algorithm> // transform()
#include <map>
#include <string>

#include <mex.h>

extern "C"
{
#include <libavutil/parseutils.h>
}

#include "ffmpeg/ffmpegException.h"
#include "ffmpeg/mxutils.h"

// tf = isframesize(val) (prevalidated)
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    std::string expr;
    bool ok;
    if (mxIsNumeric(prhs[0]))
    {
        ok = mxGetNumberOfElements(prhs[0]) == 2;
        if (ok)
        { // convert to string
            mxArray *mxStr;
            mexCallMATLAB(1, &mxStr, 1, (mxArray **)prhs, "num2str");
            expr = mxArrayToStdString(mxStr); // should look something like "###  ###"
            mxDestroyArray(mxStr);

            // replace the first space with
            auto pos = expr.find(' ');
            ok = (pos == std::string::npos);
            if (ok)
                expr[pos] = 'x';
        }
    }
    else
    {
        expr = mxArrayToStdString(prhs[0]);
        ok =( expr != "");
    }

    // initialize ffmpeg::Exception
    ffmpeg::Exception::initialize();

    int width, height; // dummy
    plhs[0] = mxCreateLogicalScalar(av_parse_video_size(&width, &height, expr.c_str()) >= 0);
}
