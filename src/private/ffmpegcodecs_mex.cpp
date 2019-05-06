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

typedef std::pair<const AVCodec *, const AVCodec *> codec_pair_t;
typedef std::map<std::string, codec_pair_t> codec_map_t;

codec_map_t map_codecs(bool enc, bool dec, bool video, bool audio, bool subtitle, bool other)
{
    codec_map_t list;

    void *opaque = nullptr;
    const AVCodec *codec;
    while ((codec = av_codec_iterate(&opaque)))
    {
        // if specified to exclude decoder or encoder, skip if matched to exclude
        bool isdec = av_codec_is_decoder(codec);
        bool isenc = av_codec_is_encoder(codec);
        if ((!dec && isdec) || (!enc && isenc))
            continue;

        // if it is one of excluded codec types, skip
        if ((!video && (codec->type == AVMEDIA_TYPE_VIDEO)) || (!audio && (codec->type == AVMEDIA_TYPE_AUDIO)) ||
            (!subtitle && (codec->type == AVMEDIA_TYPE_SUBTITLE)) ||
            (!other && (codec->type == AVMEDIA_TYPE_DATA || codec->type == AVMEDIA_TYPE_ATTACHMENT || codec->type == AVMEDIA_TYPE_UNKNOWN)))
            continue;

        // get descriptor for codec name
        const AVCodecDescriptor *desc = avcodec_descriptor_get(codec->id);

        auto elem = list.find(desc->name);
        if (elem == list.end())
        {
            codec_pair_t pair;
            if (isenc)
                pair = std::make_pair(codec, nullptr);
            if (isdec)
                pair = std::make_pair(nullptr, codec);
            list.emplace(std::make_pair(desc->name, pair));
        }
        else
        {
            if (isenc)
                elem->second.first = codec;
            if (isdec)
                elem->second.second = codec;
        }
    }
    return list;
}

void dumpOneCodec(mxArray *mxInfo, int index, const codec_pair_t &codec_pair, bool both)
{
    if (both)
    {
        mxSetField(mxInfo, index, "encoder", mxCreateLogicalScalar(codec_pair.first));
        mxSetField(mxInfo, index, "decoder", mxCreateLogicalScalar(codec_pair.second));
    }

    const AVCodec *codec = codec_pair.first ? codec_pair.first : codec_pair.second;
    const AVCodecDescriptor *desc = avcodec_descriptor_get(codec->id);
    mxSetField(mxInfo, index, "name", mxCreateString(desc->name));
    mxSetField(mxInfo, index, "long_name", mxCreateString(desc->long_name));
    mxSetField(mxInfo, index, "type", mxCreateString(av_get_media_type_string(desc->type)));
    mxSetField(mxInfo, index, "intra_only", mxCreateLogicalScalar(desc->props & AV_CODEC_PROP_INTRA_ONLY));
    mxSetField(mxInfo, index, "lossy", mxCreateLogicalScalar(desc->props & AV_CODEC_PROP_LOSSY));
    mxSetField(mxInfo, index, "lossless", mxCreateLogicalScalar(desc->props & AV_CODEC_PROP_LOSSLESS));
    mxSetField(mxInfo, index, "reorder", mxCreateLogicalScalar(desc->props & AV_CODEC_PROP_REORDER));
    mxSetField(mxInfo, index, "bitmap_sub", mxCreateLogicalScalar(desc->props & AV_CODEC_PROP_BITMAP_SUB));
    mxSetField(mxInfo, index, "text_sub", mxCreateLogicalScalar(desc->props & AV_CODEC_PROP_TEXT_SUB));

    if (!both)
    {
        int cap = codec->capabilities;
        mxSetField(mxInfo, index, "draw_horiz_band", mxCreateLogicalScalar(cap & AV_CODEC_CAP_DRAW_HORIZ_BAND));
        mxSetField(mxInfo, index, "dr1", mxCreateLogicalScalar(cap & AV_CODEC_CAP_DR1));
        mxSetField(mxInfo, index, "truncated", mxCreateLogicalScalar(cap & AV_CODEC_CAP_TRUNCATED));
        mxSetField(mxInfo, index, "delay", mxCreateLogicalScalar(cap & AV_CODEC_CAP_DELAY));
        mxSetField(mxInfo, index, "small_last_frame", mxCreateLogicalScalar(cap & AV_CODEC_CAP_SMALL_LAST_FRAME));
        mxSetField(mxInfo, index, "subframe", mxCreateLogicalScalar(cap & AV_CODEC_CAP_SUBFRAMES));
        mxSetField(mxInfo, index, "experimental", mxCreateLogicalScalar(cap & AV_CODEC_CAP_EXPERIMENTAL));
        mxSetField(mxInfo, index, "channel_conf", mxCreateLogicalScalar(cap & AV_CODEC_CAP_CHANNEL_CONF));
        mxSetField(mxInfo, index, "frame_threads", mxCreateLogicalScalar(cap & AV_CODEC_CAP_FRAME_THREADS));
        mxSetField(mxInfo, index, "slice_threads", mxCreateLogicalScalar(cap & AV_CODEC_CAP_SLICE_THREADS));
        mxSetField(mxInfo, index, "param_change", mxCreateLogicalScalar(cap & AV_CODEC_CAP_PARAM_CHANGE));
        mxSetField(mxInfo, index, "auto_threads", mxCreateLogicalScalar(cap & AV_CODEC_CAP_AUTO_THREADS));
        mxSetField(mxInfo, index, "variable_frame_size", mxCreateLogicalScalar(cap & AV_CODEC_CAP_VARIABLE_FRAME_SIZE));
        mxSetField(mxInfo, index, "avoid_probing", mxCreateLogicalScalar(cap & AV_CODEC_CAP_AVOID_PROBING));
        mxSetField(mxInfo, index, "hardware", mxCreateLogicalScalar(cap & AV_CODEC_CAP_HARDWARE));
        mxSetField(mxInfo, index, "hybrid", mxCreateLogicalScalar(cap & AV_CODEC_CAP_HYBRID));
#ifdef AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE
        mxSetField(mxInfo, index, "encoder_reordered_opaque", mxCreateLogicalScalar(cap & AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE));
#endif

        // if (avcodec_get_hw_config(c, 0))
        // {
        //     printf("    Supported hardware devices: ");
        //     for (int i = 0;; i++)
        //     {
        //         const AVCodecHWConfig *config = avcodec_get_hw_config(c, i);
        //         if (!config)
        //             break;
        //         printf("%s ", av_hwdevice_get_type_name(config->device_type));
        //     }
        //     printf("\n");
        // }

        // if (c->supported_framerates)
        // {
        //     const AVRational *fps = c->supported_framerates;

        //     printf("    Supported framerates:");
        //     while (fps->num)
        //     {
        //         printf(" %d/%d", fps->num, fps->den);
        //         fps++;
        //     }
        //     printf("\n");
        // }
        // PRINT_CODEC_SUPPORTED(c, pix_fmts, enum AVPixelFormat, "pixel formats",
        //                       AV_PIX_FMT_NONE, GET_PIX_FMT_NAME);
        // PRINT_CODEC_SUPPORTED(c, supported_samplerates, int, "sample rates", 0,
        //                       GET_SAMPLE_RATE_NAME);
        // PRINT_CODEC_SUPPORTED(c, sample_fmts, enum AVSampleFormat, "sample formats",
        //                       AV_SAMPLE_FMT_NONE, GET_SAMPLE_FMT_NAME);
        // PRINT_CODEC_SUPPORTED(c, channel_layouts, uint64_t, "channel layouts",
        //                       0, GET_CH_LAYOUT_DESC);

        // if (c->priv_class)
        // {
        //     show_help_children(c->priv_class,
        //                        AV_OPT_FLAG_ENCODING_PARAM |
        //                            AV_OPT_FLAG_DECODING_PARAM);
        // }
    }
}

mxArray *dumpToMatlab(const codec_map_t &list, bool both)
{
#define NUM_BASEFIELDS 11
    const char *basefields[NUM_BASEFIELDS] = {
        "name",
        "long_name",
        "type",
        "encoder",
        "decoder",
        "intra_only",
        "lossy",
        "lossless",
        "reorder",
        "bitmap_sub",
        "text_sub"};
    mxArray *mxInfo = mxCreateStructMatrix(list.size(), 1, NUM_BASEFIELDS, basefields);

    if (!both) // if display only encoders or decoders, add more fields
    {
        mxRemoveField(mxInfo, mxGetFieldNumber(mxInfo, "encoder"));
        mxRemoveField(mxInfo, mxGetFieldNumber(mxInfo, "decoder"));
        mxAddField(mxInfo, "draw_horiz_band");
        mxAddField(mxInfo, "dr1");
        mxAddField(mxInfo, "truncated");
        mxAddField(mxInfo, "delay");
        mxAddField(mxInfo, "small_last_frame");
        mxAddField(mxInfo, "subframe");
        mxAddField(mxInfo, "experimental");
        mxAddField(mxInfo, "channel_conf");
        mxAddField(mxInfo, "frame_threads");
        mxAddField(mxInfo, "slice_threads");
        mxAddField(mxInfo, "param_change");
        mxAddField(mxInfo, "auto_threads");
        mxAddField(mxInfo, "variable_frame_size");
        mxAddField(mxInfo, "avoid_probing");
        mxAddField(mxInfo, "hardware");
        mxAddField(mxInfo, "hybrid");
        mxAddField(mxInfo, "encoder_reordered_opaque");
    }

    int index = 0;
    for (auto elem = list.begin(); elem != list.end(); ++elem, ++index)
        dumpOneCodec(mxInfo, index, elem->second, both);
    return mxInfo;
}

// info = ffmpegformats(enc, dec, video, audio, subtitle, other)
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    // arguments are prevalidated (private function)

    // process the input arguments (all prevalidated)
    bool enc = mxIsLogicalScalarTrue(prhs[0]);
    bool dec = mxIsLogicalScalarTrue(prhs[1]);
    bool video = mxIsLogicalScalarTrue(prhs[2]);
    bool audio = mxIsLogicalScalarTrue(prhs[3]);
    bool subtitle = mxIsLogicalScalarTrue(prhs[4]);
    bool other = mxIsLogicalScalarTrue(prhs[5]);

    // initialize FFmpeg
    avformat_network_init();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif

    // initialize AVException
    AVException::initialize();

    codec_map_t list = map_codecs(enc, dec, video, audio, subtitle, other);

    // create struct array
    plhs[0] = dumpToMatlab(list, enc && dec);
}
