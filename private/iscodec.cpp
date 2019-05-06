#include <memory>
#include <algorithm> // transform()
#include <map>
#include <string>

#include <mex.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#if CONFIG_AVDEVICE
#include <libavdevice/avdevice.h>
#endif
}

#include "ffmpeg/avexception.h"
#include "ffmpeg/mxutils.h"

// tf = iscodec(val,type,encoder) (prevalidated)
// type: 0-video, 1-audio, 2-data, 3-subtitle, 4-attachment
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    // arguments are prevalidated (private function)
    auto name = mxArrayToStdString(prhs[0], true);
    AVMediaType type = (AVMediaType)(int)mxGetScalar(prhs[1]);
    bool encoder = mxIsLogicalScalarTrue(prhs[2]);

    // initialize FFmpeg
    avformat_network_init();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif

    // initialize AVException
    AVException::initialize();

    // derived from find_codec_or_die() in ffmpeg_opt.c
    AVCodec *codec = encoder ? avcodec_find_encoder_by_name(name.c_str()) : avcodec_find_decoder_by_name(name.c_str());
    if (!codec)
    {
        const AVCodecDescriptor *desc = avcodec_descriptor_get_by_name(name.c_str());
        if (desc)
            codec = encoder ? avcodec_find_encoder(desc->id) : avcodec_find_decoder(desc->id);
    }
    plhs[0] = mxCreateLogicalScalar(codec && (codec->type == type));
}
