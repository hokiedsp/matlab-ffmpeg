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

// info = ffmpeginfo_mex(filename)
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
    
    // get media file name
    char *filename = mxArrayToUTF8String(prhs[0]);
    mxAutoFree(filename);

    // open the media file
    FFmpegInputFile mediafile(filename);

    // get a dump of the media info
    plhs[0] = FFmpegInputFile::createMxInfoStruct();
    mediafile.dumpToMatlab(plhs[0]);
}
