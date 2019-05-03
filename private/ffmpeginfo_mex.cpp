#include <memory>

#include <mex.h>

#include "ffmpeg/FFmpegInputFile.h"

#define mxAutoCleanUp(p) std::unique_ptr<void, decltype(&mxFree)> cleanup_##p(p, &mxFree) // auto-deallocate the buffer

// info = ffmpeginfo_mex(filename)
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nlhs > 0 || nrhs != 1)
        mexErrMsgTxt("Takes exactly 1 input argument and produces 1 output.");
    if (!mxIsChar(prhs[0]))
        mexErrMsgTxt("Filename must be given as a character array.");

    char *filename = mxArrayToUTF8String(prhs[0]);
    mxAutoCleanUp(filename);

    FFmpegInputFile mediafile(filename);
    plhs[0] = FFmpegInputFile::createMxInfoStruct();
    mediafile.dumpToMatlab(plhs[0]);
}
