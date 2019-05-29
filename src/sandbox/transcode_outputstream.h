#pragma once

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/fifo.h>
#include <libavutil/rational.h>
#include <libavutil/eval.h>
}

struct OutputFilter;

enum OSTFinished
{
    ENCODER_FINISHED = 1,
    MUXER_FINISHED = 2,
};

enum forced_keyframes_const {
    FKF_N,
    FKF_N_FORCED,
    FKF_PREV_FORCED_N,
    FKF_PREV_FORCED_T,
    FKF_T,
    FKF_NB
};

class InputStream;

struct OutputStream
{
    int file_index;      /* T file index */
    int index;           /* T stream index in the output file */
    // int source_index;    /* T InputStream index */
    InputStream *source;
    AVStream *st;        /* T stream in the output file */
    int encoding_needed; /* T true if encoding needed for this stream */
    int frame_number;    // T

    // /* input pts and corresponding output pts
    //    for A/V sync */
    // struct InputStream *sync_ist; /* input stream to sync against */
    int64_t sync_opts;       /* OF output frame counter, could be changed to some true timestamp */ // FIXME look at frame_number
    // /* pts of the first frame encoded for this stream, used for limiting
    //  * recording time */
    int64_t first_pts;
    /* dts of the last packet sent to the muxer */
    int64_t last_mux_dts; // OF
    // // the timebase of the packets sent to the muxer
    AVRational mux_timebase; // T
    AVRational enc_timebase;

    int nb_bitstream_filters; // OF
    AVBSFContext **bsf_ctx;   // OF

    AVCodecContext *enc_ctx; // T
    AVCodecParameters *ref_par; /* associated input codec parameters with encoders options applied */
    AVCodec *enc;            // IF
    int64_t max_frames;      // T
    AVFrame *filtered_frame; // T
    AVFrame *last_frame; // OF
    int last_dropped;  // OF
    int last_nb0_frames[3]; // OF

    // void  *hwaccel_ctx;

    /* video only */
    AVRational frame_rate; // OF
    int is_cfr; // OF
    int force_fps; // OF
    int top_field_first; // OF
    int rotate_overridden;
    double rotate_override_value;

    AVRational frame_aspect_ratio; // T

    // /* forced key frames */
    int64_t forced_kf_ref_pts; // OF
    int64_t *forced_kf_pts; // T
    int forced_kf_count; // OF
    int forced_kf_index; // OF
    char *forced_keyframes; // OF
    AVExpr *forced_keyframes_pexpr; // OF
    double forced_keyframes_expr_const_values[FKF_NB]; // OF

    // /* audio only */
    int *audio_channels_map;             /* FI list of the channels id to pick from the source stream */
    int audio_channels_mapped;           /* FI number of channels in audio_channels_map */

    // char *logfile_prefix;
    FILE *logfile; // OF

    OutputFilter *filter; // T
    char *avfilter; // FI
    char *filters;         /// OF < filtergraph associated to the -filter option
    char *filters_script;  /// OF < filtergraph script associated to the -filter_script option

    AVDictionary *encoder_opts;  // T
    AVDictionary *sws_dict;      // T
    AVDictionary *swr_opts;      // T
    AVDictionary *resample_opts; // T
    char *apad;                  // T
    OSTFinished finished;        /* T no more packets should be written for this stream */
    int unavailable;             /* T true if the steram is unavailable (possibly temporarily) */
    int stream_copy;             // IF

    // // init_output_stream() has been called for this stream
    // // The encoder and the bitstream filters have been initialized and the stream
    // // parameters are set in the AVStream.
    int initialized;

    int inputs_done;

    // const char *attachment_filename;
    // int copy_initial_nonkeyframes;
    // int copy_prior_start;
    char *disposition;

    int keep_pix_fmt; // FI

    /* stats */
    // // combined size of all the packets written
    uint64_t data_size; // OF
    // number of packets send to the muxer
    uint64_t packets_written;
    // // number of frames/samples sent to the encoder
    uint64_t frames_encoded; // OF
    uint64_t samples_encoded; // OF

    /* packet quality factor */
    int quality; // OF

    int max_muxing_queue_size; // OF

    /* the packets are buffered here until the muxer is ready to be initialized */
    AVFifoBuffer *muxing_queue; // OF

    /* packet picture type */
    int pict_type; // OF

    /* frame encode sum of squared error values */
    int64_t error[4];
};

int init_output_stream(OutputStream *ost, char *error, int error_len);
void close_output_stream(OutputStream *ost);
void finish_output_stream(OutputStream *ost);
int check_recording_time(OutputStream *ost);
