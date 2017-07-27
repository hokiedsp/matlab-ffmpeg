#pragma once

#include <string>
#include <vector>
#include <thread>
#include <fstream>
#include <cstdio> // for FILE
#include <functional>

extern "C" {
#include <libavformat/avformat.h> // for AVStream
#include <libavcodec/avcodec.h>   // for AVCodecContext, AVCodec, AVSubtitle, AVBSFContext, AVCodecParameters, AVCodecParserContext
#include <libavutil/frame.h>      // for AVFrame
#include <libavutil/rational.h>   // for AVRational
#include <libavutil/eval.h>       // for AVExpr
#include <libavutil/fifo.h>       // for AVFifoBuffer
}

#include "ffmpegBase.h"

namespace ffmpeg
{

struct InputStream;
struct OutputFile;

enum OSTFinished
{
   ENCODER_FINISHED = 1,
   MUXER_FINISHED = 2,
};

enum forced_keyframes_const
{
   FKF_N,
   FKF_N_FORCED,
   FKF_PREV_FORCED_N,
   FKF_PREV_FORCED_T,
   FKF_T,
   FKF_NB
};

struct OutputStream : public ffmpegBase
{
   OutputFile &file; /* file index */
   int index;        /* stream index in the output file */

   InputStream *source_ist;        /* InputStream index */

   bool stream_copy;     /* true if stream is just copied */
   bool encoding_needed; /* true if encoding needed for this stream */
   int64_t max_frames;
   bool copy_prior_start;

   /* input pts and corresponding output pts
       for A/V sync */
   InputStream *sync_ist; /* input stream to sync against */

   /* dts of the last packet sent to the muxer */
   int64_t last_mux_dts;

   AVStream *st; /* stream in the output file */
   AVCodec *enc;
   AVCodecContext *enc_ctx;
   AVCodecParameters *ref_par; /* associated input codec parameters with encoders options applied */
   AVDictionary *encoder_opts; // encoder options

   AVBSFContext **bsf_ctx;
   int nb_bitstream_filters;
   uint8_t *bsf_extradata_updated;

   std::string disposition;
   int max_muxing_queue_size;

   int source_index; /* InputStream index */
   int frame_number;

   int64_t sync_opts; /* output frame counter, could be changed to some true timestamp */ // FIXME look at frame_number
   /* pts of the first frame encoded for this stream, used for limiting
     * recording time */
   int64_t first_pts;

   AVFrame *filtered_frame;
   AVFrame *last_frame;
   int last_dropped;
   int last_nb0_frames[3];

   void *hwaccel_ctx;

   /* video only */
   AVRational frame_rate;
   int is_cfr;
   int force_fps;
   int top_field_first;
   int rotate_overridden;

   AVRational frame_aspect_ratio;

   /* forced key frames */
   int64_t *forced_kf_pts;
   int forced_kf_count;
   int forced_kf_index;
   std::string forced_keyframes;
   AVExpr *forced_keyframes_pexpr;
   double forced_keyframes_expr_const_values[FKF_NB];

   /* audio only */
   std::vector<int> audio_channels_map; /* list of the channels id to pick from the source stream */

   std::string logfile_prefix;
   std::ofstream logfile;

   OutputFilter *filter;
   std::string avfilter;
   std::string filters;        ///< filtergraph associated to the -filter option
   std::string filters_script; ///< filtergraph script associated to the -filter_script option

   AVDictionary *sws_dict;
   AVDictionary *swr_opts;
   AVDictionary *resample_opts;
   std::string apad;
   OSTFinished finished; /* no more packets should be written for this stream */
   bool unavailable;     /* true if the stream is unavailable (possibly temporarily) */

   // init_output_stream() has been called for this stream
   // The encoder and the bitstream filters have been initialized and the stream
   // parameters are set in the AVStream.
   bool initialized;

   const std::string attachment_filename;
   bool copy_initial_nonkeyframes;

   bool keep_pix_fmt;

   AVCodecParserContext *parser;
   AVCodecContext *parser_avctx;

   /* stats */
   // combined size of all the packets written
   uint64_t data_size;
   // number of packets send to the muxer
   uint64_t packets_written;
   // number of frames/samples sent to the encoder
   uint64_t frames_encoded;
   uint64_t samples_encoded;

   /* packet quality factor */
   int quality;

   /* the packets are buffered here until the muxer is ready to be initialized */
   AVFifoBuffer *muxing_queue;

   /* packet picture type */
   int pict_type;

   /* frame encode sum of squared error values */
   int64_t error[4];

   std::string choose_pix_fmts();
   std::string choose_sample_fmts();
   std::string choose_channel_layouts();
   std::string choose_sample_rates();

   OutputStream::OutputStream(OutputFile &f, const int i, AVFormatContext *oc, const AVMediaType type, OptionsContext &o, InputStream *src = NULL);
   ~OutputStream();

   //InputStream *get_input_stream(OutputStream *ost)
   InputStream *get_input_stream();

   //void parse_forced_key_frames(char *kf, OutputStream *ost, AVCodecContext *avctx)
   void parse_forced_key_frames(char *kf, AVCodecContext *avctx);

   //int init_output_stream(OutputStream *ost, char *error, int error_len);
   void init_output_stream(std::string &error);

   void close_output_stream();

   //void finish_output_stream(OutputStream *ost)
   void finish();

   void clear_stream();

 private:
   int get_preset_file_2(const std::string &preset_name, const std::string &codec_name, AVIOContext *&s);
   uint8_t *get_line(AVIOContext *s);

   static AVPixelFormat choose_pixel_fmt(AVStream *st, AVCodecContext *enc_ctx, AVCodec *codec, AVPixelFormat target);
   static const AVPixelFormat *get_compliance_unofficial_pix_fmts(AVCodecID codec_id, const AVPixelFormat default_formats[]);
};

typedef std::vector<OutputStream> OutputStreams;
typedef std::vector<std::reference_wrapper<OutputStream>> OutputStreamRefs;
}
