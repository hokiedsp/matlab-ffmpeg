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

// tf = isframerate(val) (prevalidated)
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    std::string expr;
    if (mxIsNumeric(prhs[0]))
    {
        // convert to string
        mxArray *mxStr;
        mexCallMATLAB(1, &mxStr, 1, (mxArray**)prhs, "num2str");
        expr = mxArrayToStdString(mxStr);
        mxDestroyArray(mxStr);
    }
    else
    {
        expr = mxArrayToStdString(prhs[0]);
    }

    // initialize ffmpeg::Exception
    ffmpeg::Exception::initialize();

    AVRational rate; // dummy
    plhs[0] = mxCreateLogicalScalar(av_parse_video_rate(&rate, expr.c_str())>=0);
}
