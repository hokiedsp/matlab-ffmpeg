#pragma once

#include <vector>
#include <string>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/fifo.h>
}

// #include "transcode_inputfile.h"
#include "transcode_filter.h"
#include "transcode_hw.h"

struct InputFile;

struct InputStream
{
    static bool bitexact;

    InputStream(InputFile &file, int index, bool open_codec = true, const AVDictionary *codec_opts = NULL);
    std::string IdString() const;
    void setDecoder(const std::string &name_str);
    void setHWAccel(const std::string &hwaccel, const std::string &hwaccel_device, const std::string &hwaccel_output_format);

    void initDecoder(const AVDictionary *codec_opts = NULL);
    void openDecoder();

    InputFile &file; // T

    int process_input_packet(const AVPacket *pkt, bool no_eof); // from transcode.cpp

    void sub2video_heartbeat(int64_t pts); // from transcode_inputfile.cpp
    void sub2video_update(AVSubtitle *sub);

    AVStream *st;        // T
    bool discard;        /* T true if stream data should be discarded */
                         //     int user_set_discard;
    int decoding_needed; /* T non zero if the packets must be decoded in 'raw_fifo', see DECODING_FOR_* */
#define DECODING_FOR_OST 1
#define DECODING_FOR_FILTER 2

    AVCodecContext *dec_ctx; // T
    AVCodec *dec;            // IF
    AVFrame *decoded_frame;
    AVFrame *filter_frame; /* a ref of decoded_frame, to be sent to filters */

    int64_t start; /* T time when read started */
    /* predicted dts of the next packet read for this stream or (when there are
                        * several frames in a packet) of the next frame in current packet (in AV_TIME_BASE units) */
    int64_t next_dts; // IF
    int64_t dts;      ///IF < dts of the last packet read for this stream (in AV_TIME_BASE units)

    int64_t next_pts;         /// IF < synthetic pts for the next decode frame (in AV_TIME_BASE units)
    int64_t pts;              /// IF < current pts of the decoded frame  (in AV_TIME_BASE units)
    int wrap_correction_done; // IF

    int64_t filter_in_rescale_delta_last;

    int64_t min_pts; /* IF pts with the smallest value in a current stream */
    int64_t max_pts; /* IF pts with the higher value in a current stream */

    // when forcing constant input framerate through -r,
    // this contains the pts that will be given to the next decoded frame
    int64_t cfr_next_pts;

    int64_t nb_samples; /* IF number of samples in the last decoded audio frame before looping */

    double ts_scale; // IF
    bool saw_first_ts;
    AVDictionary *decoder_opts; // IF
    AVRational framerate;       /* IF framerate forced with -r */
    int top_field_first;
    int guess_layout_max;

    int autorotate;

    int fix_sub_duration;
    struct
    { /* previous decoded subtitle and related variables */
        int got_output;
        int ret;
        AVSubtitle subtitle;
    } prev_sub;

    struct sub2video_s
    {
        int64_t last_pts;
        int64_t end_pts;
        AVFifoBuffer *sub_queue; ///< queue of AVSubtitle* before filter init
        AVFrame *frame;
        int w, h;
    };
    sub2video_s sub2video;

    //     int dr1;

    //     /* decoded data from this stream goes into all those filters
    //      * currently video and audio only */
    std::vector<InputFilter *> filters;

    int reinit_filters; // FI

    /* hwaccel options */
    enum HWAccelID hwaccel_id;
    enum AVHWDeviceType hwaccel_device_type;
    char *hwaccel_device;
    enum AVPixelFormat hwaccel_output_format; // HW

    //     /* hwaccel context */
    //     void  *hwaccel_ctx;
    void (*hwaccel_uninit)(AVCodecContext *s); // T
    int (*hwaccel_get_buffer)(AVCodecContext *s, AVFrame *frame, int flags);
    int (*hwaccel_retrieve_data)(AVCodecContext *s, AVFrame *frame);
    AVPixelFormat hwaccel_pix_fmt;
    AVPixelFormat hwaccel_retrieved_pix_fmt;
    AVBufferRef *hw_frames_ctx;

    /* stats */
    // combined size of all the packets read
    uint64_t data_size; // IF
    /* number of packets successfully read for this stream */
    uint64_t nb_packets; // IF
                         //     // number of frames/samples retrieved from the decoder
    uint64_t frames_decoded;
    uint64_t samples_decoded;

    int64_t *dts_buffer;
    int nb_dts_buffer;

    int got_output; // T

private:
    void hw_device_setup_for_decode();
    int decode_audio(AVPacket *pkt, int *got_output, int *decode_failed);
    int decode_video(AVPacket *pkt, int *got_output, int64_t *duration_pts, int eof, int *decode_failed);
    int transcode_subtitles(AVPacket *pkt, int *got_output, int *decode_failed);
    void send_filter_eof();
    void check_decode_result(int *got_output, int ret);
    int send_frame_to_filters(AVFrame *decoded_frame);

    // likely to be privatized
    static int decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *pkt);
    static int get_buffer(AVCodecContext *s, AVFrame *frame, int flags);
    static AVPixelFormat get_format(AVCodecContext *s, const AVPixelFormat *pix_fmts);

    void sub2video_flush();
    void sub2video_push_ref(int64_t pts);
    int sub2video_get_blank_frame();
};

typedef std::vector<InputStream> InputStreamVect;
