#include <memory>
#include <algorithm> // transform()
#include <map>
#include <string>

#include <mex.h>
#include "ffmpeg/avexception.h"

extern "C"
{
#include <libavformat/avformat.h>
#if CONFIG_AVDEVICE
#include <libavdevice/avdevice.h>
#endif
}

enum show_muxdemuxers_t
{
    SHOW_DEFAULT,
    SHOW_DEMUXERS,
    SHOW_MUXERS,
};

struct format_info_t
{
    bool decode;
    bool encode;
    bool device;
    const char *long_name;

    format_info_t() : decode(false), encode(false), device(false), long_name(nullptr){};
    format_info_t(bool is_enc, bool is_dev, const char *name) : decode(!is_enc), encode(is_enc), device(is_dev), long_name(name){};
};

typedef std::map<std::string, format_info_t> format_map_t;

static int is_device(const AVClass *avclass)
{
    if (!avclass)
        return 0;
    return AV_IS_INPUT_DEVICE(avclass->category) || AV_IS_OUTPUT_DEVICE(avclass->category);
}

mxArray *show_formats_devices(bool device_only, show_muxdemuxers_t muxdemuxers)
{
    format_map_t list;
    int is_dev;

    if (muxdemuxers != SHOW_DEMUXERS)
    {
        void *ofmt_opaque = nullptr;
        const AVOutputFormat *ofmt;
        while ((ofmt = av_muxer_iterate(&ofmt_opaque)))
        {
            is_dev = is_device(ofmt->priv_class);
            if (!is_dev && device_only)
                continue;
            list.emplace(std::make_pair(ofmt->name, format_info_t(true, is_dev, ofmt->long_name)));
        }
    }

    if (muxdemuxers != SHOW_MUXERS)
    {
        void *ifmt_opaque = NULL;
        const AVInputFormat *ifmt = NULL;
        while ((ifmt = av_demuxer_iterate(&ifmt_opaque)))
        {
            is_dev = is_device(ifmt->priv_class);
            if (!is_dev && device_only)
                continue;

            auto elem = list.find(ifmt->name);
            if (elem == list.end())
                list.emplace(std::make_pair(ifmt->name, format_info_t(false, is_dev, ifmt->long_name)));
            else
                elem->second.decode = true;
        }
    }

    const char *basefields[] = {"name", "long_name"};
    mxArray *ret = mxCreateStructMatrix(list.size(), 1, 2, basefields);
    if (muxdemuxers == SHOW_DEFAULT)
    {
        mxAddField(ret, "mux");
        mxAddField(ret, "demux");
    }
    if (!device_only)
        mxAddField(ret, "device");

    int index = 0;
    for (auto it = list.begin(); it != list.end(); ++it, ++index)
    {
        mxSetField(ret, index, "name", mxCreateString(it->first.c_str()));
        mxSetField(ret, index, "long_name", mxCreateString(it->second.long_name));
        if (muxdemuxers == SHOW_DEFAULT)
        {
            mxSetField(ret, index, "mux", mxCreateLogicalScalar(it->second.encode));
            mxSetField(ret, index, "demux", mxCreateLogicalScalar(it->second.decode));
        }
        if (!device_only)
            mxSetField(ret, index, "device", mxCreateLogicalScalar(it->second.device));
    }

    return ret;
}

// info = ffmpegformats(muxer, demuxer, deviceonly)
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nrhs != 3 || nlhs != 1)
        mexErrMsgTxt("Takes up to 2 input arguments and 1 output argument.");

    // process the input arguments (all prevalidated)
    bool muxer = mxIsLogicalScalarTrue(prhs[0]);
    bool demuxer = mxIsLogicalScalarTrue(prhs[1]);
    bool deviceonly = mxIsLogicalScalarTrue(prhs[2]);
    show_muxdemuxers_t muxdemuxers = !muxer ? SHOW_DEMUXERS : !demuxer ? SHOW_MUXERS : SHOW_DEFAULT;

    // initialize FFmpeg
    avformat_network_init();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif

    // initialize AVException
    AVException::initialize();

    // create struct array
    plhs[0] = show_formats_devices(deviceonly, muxdemuxers);
}
