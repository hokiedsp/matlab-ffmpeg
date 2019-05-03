#include <memory>

#include <mex.h>

extern "C"
{
#include <libavformat/avformat.h>
#if CONFIG_AVDEVICE
#include <libavdevice/avdevice.h>
#endif
}

#include "ffmpeg/FFmpegInputFile.h"
#include "ffmpeg/avexception.h"
#include "ffmpeg/ffmpeg_utils.h"

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

    // initialize AVException
    AVException::initialize();

    // // testing logging capability
    // AVException::av_log_level = AV_LOG_INFO;
    // AVException::log_fcn = [](const std::string &msg) { mexPrintf("%s", msg.c_str()); };
    // AVException::log(AV_LOG_INFO, "executing ffmpegmediatypes...");

    char *filename = mxArrayToUTF8String(prhs[0]);
    mxAutoFree(filename);

    FFmpegInputFile mediafile(filename);
    auto types = mediafile.getMediaTypes();

    plhs[0] = mxCreateCellMatrix(types.size(), 1);
    for (int i = 0; i < types.size(); ++i)
        mxSetCell(plhs[0], i, mxCreateString(types[i].c_str()));
}
