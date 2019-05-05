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
#include "ffmpeg/mxutils.h"

// info = ffmpeginfo_mex(filenames)
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    // retrieve file names (prevalidated)
    auto filenames = mxParseStringArgs((int)mxGetNumberOfElements(prhs[0]), (const mxArray **)mxGetData(prhs[0]));

    // initialize FFmpeg
    avformat_network_init();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif

    // initialize AVException
    AVException::initialize();

    // initialize the output struct
    plhs[0] = FFmpegInputFile::createMxInfoStruct(filenames.size());

    int index = 0;
    for (auto pfile = filenames.begin(); pfile != filenames.end(); ++pfile)
    {
        // open the media file
        FFmpegInputFile mediafile(pfile->c_str());

        // get a dump of the media info
        mediafile.dumpToMatlab(plhs[0], index++);
    }
}
