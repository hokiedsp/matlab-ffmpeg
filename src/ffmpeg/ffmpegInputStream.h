#pragma once

#include <string>
#include <vector>
#include <queue>
#include <memory>
#include <functional>

extern "C" {
#include <libavformat/avformat.h> // for AVStream
#include <libavcodec/avcodec.h>   // for AVCodecContext, AVCodec, AVSubtitle
// #include <libavutil/frame.h>      // for AVFrame
#include <libavutil/dict.h> // for AVDictionary
#include <libavutil/time.h>
// #include <libavutil/rational.h>   // for AVRational
// #include <libavutil/pixfmt.h>     // for AVPixelFormat
// #include <libavutil/buffer.h>     // for AVBufferRef
#include <libswscale/swscale.h>
}

#include "ffmpegBase.h"
#include "ffmpegOptionsContextInput.h"
#include "ffmpegPtrs.h"
#include "ffmpegFilterGraph.h"

namespace ffmpeg
{
enum HWAccelID
{
   HWACCEL_NONE = 0,
   HWACCEL_AUTO,
   HWACCEL_VDPAU,
   HWACCEL_DXVA2,
   HWACCEL_VDA,
   HWACCEL_VIDEOTOOLBOX,
   HWACCEL_QSV,
   HWACCEL_VAAPI,
   HWACCEL_CUVID,
};

struct HWAccel
{
   const std::string name;
   int (*init)(AVCodecContext *s);
   HWAccelID id;
   AVPixelFormat pix_fmt;
};
typedef std::vector<HWAccel> HWAccels;

struct InputFile; // concretely defined in ffmpegInputFile.h

struct InputStream : public ffmpegBase
{
  static float dts_delta_threshold;
  static float dts_error_threshold;
  InputStream(InputFile &infile, const int i, const InputOptionsContext &o);
  // ~InputStream();

  virtual void init();

  virtual void set_option(const InputOptionsContext &o);

  // compare given dictionary to decoder_opts and remove duplicates from the given
  void remove_used_opts(AVDictionary *&opts);

  int init_stream(std::string &error);
  virtual int prepare_packet(const AVPacket *pkt, bool no_eof);

  virtual int64_t get_duration(const bool has_audio, AVRational *&tb) const
  {
    // for non-audio streams, AudioInputStream && VideoInputStream overload this function

    // return the pointer to the stream's time base
    tb = &(st->time_base);

    // grab the duration only if specified in the stream object
    if (!has_audio && st->avg_frame_rate.num)
      return av_rescale_q(1, st->avg_frame_rate, st->time_base) + max_pts - min_pts;
    else
      return 1;
   }

   void assert_emu_dts() const
   {
      int64_t pts = av_rescale(dts, 1000000, AV_TIME_BASE);
      int64_t now = av_gettime_relative() - start;
      if (pts > now)
         throw ffmpegException(AVERROR(EAGAIN));
   }

   virtual int flush(bool no_eof = true); // flush decoder

   virtual bool has_audio_samples() const { return false; }

   virtual bool process_packet_time(AVPacket &pkt, int64_t &ts_offset, int64_t &last_ts);

   virtual void close(); // close the stream

   AVMediaType get_codec_type() const { return dec_ctx->codec_type; }
   AVMediaType get_stream_type() const { return st->codecpar->codec_type; }
   int check_stream_specifier(const std::string spec);
   void input_to_filter(InputFilter &new_filter); // from ffmpeg_opt.cpp::init_input_filter()

   bool unused_stream(const AVMediaType type) { return get_codec_type() == type && discard; }

   virtual AVRational get_framerate() const;
   virtual AVRational get_time_base() const { return st->time_base; }
   virtual AVRational get_sar() const { return st->sample_aspect_ratio.num ? st->sample_aspect_ratio : dec_ctx->sample_aspect_ratio; };

   virtual std::string get_buffer_filt_args() const { return ""; }

   virtual bool auto_rotate() const { return false; }
   double get_rotation();

   InputFile &file;
   AVCodec *dec;
   CodecCtxPtr dec_ctx;
   AVFrame *decoded_frame;
   AVFrame *filter_frame; /* a ref of decoded_frame, to be sent to filters */
   AVStream *st;

 protected:
   static int64_t decode_error_stat[2];

   DictPtr decoder_opts;

   bool discard; /* true if stream data should be discarded */
   AVDiscard user_set_discard;
   int64_t nb_samples; /* number of samples in the last decoded audio frame before looping */
   int64_t min_pts;    /* pts (presentation time stamp) with the smallest value in a current stream */
   int64_t max_pts;    /* pts with the higher value in a current stream */
   double ts_scale;    // the input ts scale
   
   bool saw_first_ts;

   int decoding_needed; /* non zero if the packets must be decoded in 'raw_fifo', see DECODING_FOR_* */
#define DECODING_FOR_OST 1
#define DECODING_FOR_FILTER 2

   bool wrap_correction_done;

   //OutputStreamRefs osts; // direct destination output streams

   /* predicted dts of the next packet read for this stream or (when there are
     * several frames in a packet) of the next frame in current packet (in AV_TIME_BASE units) */
   int64_t next_dts;
   int64_t dts; ///< dts of the last packet read for this stream (in AV_TIME_BASE units)

   int64_t next_pts; ///< synthetic pts for the next decode frame (in AV_TIME_BASE units)
   int64_t pts;      ///< current pts of the decoded frame  (in AV_TIME_BASE units)

   //the decoding time stamp (DTS) and presentation time stamp (PTS)

   // combined size of all the packets read
   uint64_t data_size;
   /* number of packets successfully read for this stream */
   uint64_t nb_packets;

   /* decoded data from this stream goes into all those filters
     * currently video and audio only */
   InputFilterPtrs filters;
   int reinit_filters;

   int64_t       start;     /* time when read started */
   uint64_t frames_decoded;

   static AVPixelFormat get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts);
   static int get_buffer(AVCodecContext *s, AVFrame *frame, int flags);

   // to be called by InputStream::get_format()
   virtual bool get_hwaccel_format(const AVPixelFormat *pix_fmt, bool &unknown) { return false; }
   virtual int get_stream_buffer(AVCodecContext *s, AVFrame *frame, int flags);

   int send_filter_eof();
   virtual int decode_packet(const AVPacket *inpkt, bool repeating, bool &got_output) { return 0; };
   virtual void check_decode_result(const bool got_output, const int ret);

   static int decode(AVCodecContext *avctx, AVFrame *frame, bool &got_frame, AVPacket *pkt);
};

typedef std::vector<std::unique_ptr<InputStream>> InputStreams;
typedef std::vector<InputStream*> InputStreamPtrs;

///////////////////////////////////////////////////////////////////////////////////////////////////

struct VideoInputStream : public InputStream
{
   VideoInputStream(InputFile &infile, const int i, const InputOptionsContext &o);
   //   ~VideoInputStream();

   virtual void close(); // close the stream

   virtual int64_t get_duration(const bool has_audio, AVRational *&tb) const
   {
      tb = &(st->time_base);

      if (!has_audio)
      {
         if (framerate.num)
            return av_rescale_q(1, framerate, st->time_base) + max_pts - min_pts;
         else if (st->avg_frame_rate.num)
            return av_rescale_q(1, st->avg_frame_rate, st->time_base) + max_pts - min_pts;
      }
      else
         return 1;
   }

   virtual int prepare_packet(const AVPacket *pkt, bool no_eof);

   virtual AVRational get_framerate() const;
   virtual AVRational get_time_base() const { return framerate.num ? av_inv_q(framerate) : st->time_base; }

   virtual std::string get_buffer_filt_args() const
   {
      AVRational fr = get_framerate();
      AVRational tb = get_time_base();
      AVRational sar = get_sar();
      if (!sar.den)
         sar = {0, 1};

      std::ostringstream args;
      args << "video_size=" << resample_width << "x" << resample_height
           << ":pix_fmt=" << (hwaccel_retrieve_data ? hwaccel_retrieved_pix_fmt : resample_pix_fmt)
           << ":time_base=" << tb.num << "/" << tb.den << ":pixel_aspect=" << sar.num << "/" << sar.den
           << ":sws_param=flags=" << SWS_BILINEAR + ((dec_ctx->flags & AV_CODEC_FLAG_BITEXACT) ? SWS_BITEXACT : 0);
      if (fr.num && fr.den)
         args << ":frame_rate=" << fr.num << "/" << fr.den;
      return args.str();
   }

   virtual bool auto_rotate() const { return autorotate; }
   AVBufferRef *get_hw_frames_ctx() const { return hw_frames_ctx; }
 protected:
   virtual int decode_packet(const AVPacket *inpkt, bool repeating, bool &got_output);

   // to be called by InputStream::get_format()
   bool get_hwaccel_format(const AVPixelFormat *pix_fmt, bool &unknown);

   virtual int get_stream_buffer(AVCodecContext *s, AVFrame *frame, int flags);

 private:
   AVRational framerate; /* framerate forced with -r */
   int resample_height;
   int resample_width;
   int resample_pix_fmt;
   bool autorotate;    // true then automatically insert correct rotate filters
   int top_field_first;

   /* hwaccel options */
   HWAccelID hwaccel_id;
   std::string hwaccel_device;
   AVPixelFormat hwaccel_output_format;
   AVPixelFormat hwaccel_pix_fmt;

   /* hwaccel context */
   void *hwaccel_ctx;
   HWAccelID active_hwaccel_id;
   void (*hwaccel_uninit)(AVCodecContext *s);
   int (*hwaccel_get_buffer)(AVCodecContext *s, AVFrame *frame, int flags);
   int (*hwaccel_retrieve_data)(AVCodecContext *s, AVFrame *frame);
   AVPixelFormat hwaccel_retrieved_pix_fmt;
   AVBufferRef *hw_frames_ctx;

   int64_t *dts_buffer;
   int nb_dts_buffer;

   static const HWAccels hwaccels;
  static const HWAccel *get_hwaccel(AVPixelFormat pix_fmt);
};

///////////////////////////////////////////////////////////////////////////////////////////////////

struct AudioInputStream : public InputStream
{
   AudioInputStream(InputFile &infile, const int i, const InputOptionsContext &o);
   //   ~AudioInputStream();

   bool has_audio_samples() const { return nb_samples; }

   virtual int64_t get_duration(const bool has_audio, AVRational &tb)
   {
      if (has_audio && nb_samples)
      {
         AVRational sample_rate = {1, dec_ctx->sample_rate};
         return av_rescale_q(nb_samples, sample_rate, st->time_base) + max_pts - min_pts;
      }
      else
      {
         return 1;
      }
   }

virtual int prepare_packet(const AVPacket *pkt, bool no_eof);

 protected:
   virtual int decode_packet(const AVPacket *inpkt, bool repeating, bool &got_output);
   int decode_audio(AVPacket *pkt, int &got_output);

 private:
   int guess_layout_max;

   int resample_sample_fmt;
   int resample_sample_rate;
   int resample_channels;
   uint64_t resample_channel_layout;

   uint64_t samples_decoded;

   int64_t filter_in_rescale_delta_last;

   bool guess_input_channel_layout(); // returns true if audio channel layout is given or guessed
};

///////////////////////////////////////////////////////////////////////////////////////////////////

struct DataInputStream : public InputStream
{
   DataInputStream(InputFile &infile, const int i, const InputOptionsContext &o);

 protected:
   int fix_sub_duration;

   virtual int decode_packet(const AVPacket *inpkt, bool repeating, bool &got_output) { return 0; }
};

struct SubtitleInputStream : public DataInputStream
{
   SubtitleInputStream(InputFile &infile, const int i, const InputOptionsContext &o);
   int sub2video_prepare(); // called by FilterGraph

   virtual std::string get_buffer_filt_args() const
   {
      AVRational fr = get_framerate();
      AVRational tb = get_time_base();
      AVRational sar = get_sar();
      if (!sar.den)
         sar = {0, 1};

      std::ostringstream args;
      args << "video_size=" << resample_width << "x" << resample_height << ":pix_fmt=" << resample_pix_fmt
           << ":time_base=" << tb.num << "/" << tb.den << ":pixel_aspect=" << sar.num << "/" << sar.den
           << ":sws_param=flags=" << SWS_BILINEAR + ((dec_ctx->flags & AV_CODEC_FLAG_BITEXACT) ? SWS_BITEXACT : 0);
      if (fr.num && fr.den)
         args << ":frame_rate=" << fr.num << "/" << fr.den;
      return args.str();
   }

   virtual bool auto_rotate() const { return autorotate; }

   int dr1;

   //   ~SubtitleInputStream();

   virtual int prepare_packet(const AVPacket *pkt, bool no_eof);
 protected:
   virtual int decode_packet(const AVPacket *inpkt, bool repeating, bool &got_output);

 private:
   AVSubtitle subtitle;

   int resample_height;
   int resample_width;
   int resample_pix_fmt;

   bool autorotate;    // true then automatically insert correct rotate filters

   /* sub2video hack:
   Convert subtitles to video with alpha to insert them in filter graphs.
   This is a temporary solution until libavfilter gets real subtitles support.
 */
   int sub2video_get_blank_frame();

   void sub2video_push_ref(int64_t pts);

   static void sub2video_copy_rect(AVFrame *frame, AVSubtitleRect *r);
   static void sub2video_copy_rect(uint8_t *dst, int dst_linesize, int w, int h, AVSubtitleRect *r);

   void sub2video_update(AVSubtitle *sub);

   void sub2video_heartbeat(int64_t pts);

   void sub2video_flush();

   /* end of sub2video hack */

   int transcode_subtitles(AVPacket *pkt, bool &got_output);

 private:
   struct prev_sub_s
   { /* previous decoded subtitle and related variables */
      bool got_output;
      int ret;
      AVSubtitle subtitle;
      prev_sub_s() : got_output(false), ret(0) {}
   } prev_sub;

   struct sub2video_s
   {
      int64_t last_pts;
      int64_t end_pts;
      AVFrame *frame;
      int w, h;
      sub2video_s() : last_pts(0), end_pts(0), frame(NULL), w(0), h(0) {}
   } sub2video;
};
}
