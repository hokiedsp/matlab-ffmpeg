#include <memory>

#include <mex.h>

extern "C"
{
#include <libavformat/avformat.h>
#if CONFIG_AVDEVICE
#include <libavdevice/avdevice.h>
#endif
}

#include "utils/ffmpegMxProbe.h"
#include <ffmpegException.h>
#include "utils/mxutils.h"

// types = ffmpegmediatypes(filename)
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nlhs > 1 || nrhs != 1)
        mexErrMsgTxt("Takes exactly 1 input argument and produces 1 output.");
    if (!mxIsChar(prhs[0]))
        mexErrMsgTxt("Filename must be given as a character array.");

    // initialize FFmpeg
    avformat_network_init();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif

    // initialize ffmpeg::Exception
    ffmpeg::Exception::initialize();

    // // testing logging capability
    // ffmpeg::Exception::av_log_level = AV_LOG_INFO;
    // ffmpeg::Exception::log_fcn = [](const std::string &msg) { mexPrintf("%s", msg.c_str()); };
    // ffmpeg::Exception::log(AV_LOG_INFO, "executing ffmpegmediatypes...");

    char *filename = mxArrayToUTF8String(prhs[0]);
    mxAutoFree(filename);

    ffmpeg::MxProbe mediafile(filename);
    auto types = mediafile.getMediaTypes();

    plhs[0] = mxCreateCellMatrix(types.size(), 1);
    for (int i = 0; i < types.size(); ++i)
        mxSetCell(plhs[0], i, mxCreateString(types[i].c_str()));
}
