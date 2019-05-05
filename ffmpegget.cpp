#include <memory>
#include <algorithm>

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
#include "ffmpeg/mxutils.h"

const char *pnames[] = {
    "duration"};

// propvalue = ffmpegget(filename, propname)
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nrhs < 1 || nlhs > nrhs - 1)
        mexErrMsgTxt("Takes exactly more than 1 input argument.");
    if (!mxIsChar(prhs[0]))
        mexErrMsgTxt("Filename must be given as a character array.");
    if (!mxIsChar(prhs[1]))
        mexErrMsgTxt("Property name must be given as a character array.");

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

    int nargs = nrhs - 1;
    if (nargs > nlhs)
        nargs = nlhs > 0 ? nlhs : 1;

    for (int i = 0; i < nargs; ++i)
    {
        char *pname_str = mxArrayToUTF8String(prhs[i+1]);
        std::string pname(pname_str);
        mxFree(pname_str);
        std::transform(pname.begin(), pname.end(), pname.begin(), ::tolower); // lower case

        if (pname == "duration")
        {
            plhs[i] = mxCreateDoubleScalar(mediafile.getDuration());
        }
        else if (pname == "videoframerate")
        {
            try
            {
                plhs[i] = mxCreateDoubleScalar(mediafile.getVideoFrameRate());
            }
            catch (const AVException &)
            {
                plhs[i] = mxCreateDoubleMatrix(0, 0, mxREAL);
            }
        }
        else
        {
            mexErrMsgIdAndTxt("ffmpeggetprop:invalidName", "Property %s does not exist.", pname.c_str());
        }
    }
}
