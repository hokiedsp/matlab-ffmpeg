#include <memory>
#include <algorithm> // transform()
#include <map>
#include <string>

#include <mex.h>

extern "C"
{
#include <libavutil/pixdesc.h>
}

#include "ffmpeg/ffmpegException.h"
#include "ffmpeg/mxutils.h"

// tf = ispixfmt(val) (prevalidated)
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    std::string expr = mxArrayToStdString(prhs[0]);

    // initialize ffmpeg::Exception
    ffmpeg::Exception::initialize();

    // try to convert string to av_get_pix_fmt
    plhs[0] = mxCreateLogicalScalar(av_get_pix_fmt(expr.c_str()) != AV_PIX_FMT_NONE);
}
