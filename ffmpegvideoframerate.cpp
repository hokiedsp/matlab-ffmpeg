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
#include "ffmpeg/mxutils.h"

// function fps = ffmpegvideoframerate(infile,stream)
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nlhs > 1 || !(nrhs == 1 || nrhs == 2))
        mexErrMsgTxt("Takes 1 or 2 input arguments and produces 1 output.");
    if (!mxIsChar(prhs[0]))
        mexErrMsgTxt("Filename must be given as a character array.");

    // initialize FFmpeg
    avformat_network_init();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif

    // initialize AVException
    AVException::initialize();

    double fps;
    try
    { // open the media file
        char *filename = mxArrayToUTF8String(prhs[0]);
        mxAutoFree(filename);
        FFmpegInputFile mediafile(filename);

        // retrieve the stream
        if (nrhs == 1) // best video stream
        {
            fps = mediafile.getVideoFrameRate();
        }
        else if (mxIsChar(prhs[1]))
        {
            char *spec = mxArrayToUTF8String(prhs[1]);
            mxAutoFree(spec);
            fps = mediafile.getVideoFrameRate(spec);
        }
        else
        {
            if (!(mxIsNumeric(prhs[1]) && mxIsScalar(prhs[1]) && !mxIsComplex(prhs[1])))
                mexErrMsgTxt("Stream index must be given by an integer.");

            double sidxd = mxGetScalar(prhs[1]);
            int sidx = (int)sidxd;
            if (sidxd != (double)sidx)
                mexErrMsgTxt("Stream index must be given by an integer.");

            fps = mediafile.getVideoFrameRate(sidx);
        }
    }
    catch (const AVException &e)
    {
        mexErrMsgIdAndTxt("ffmpegvideoframerate:mexError","%s",e.what());
    }
    plhs[0] = mxCreateDoubleScalar(fps);
}
