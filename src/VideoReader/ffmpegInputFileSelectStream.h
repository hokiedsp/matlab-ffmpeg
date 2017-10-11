#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>

extern "C" {
#include <libavformat/avformat.h> // for AVFormatContext
#include <libavutil/avutil.h>     // for AVMediaType
#include <libavutil/rational.h>   // for AVRational
}

#include "../Common/ffmpegBase.h"
#include "../Common/ffmpegPtrs.h"
#include "ffmpegAVPacketBuffer.h"
#include "ffmpegAVFramePtrBuffer.h"

namespace ffmpeg
{

// InputFileSelectStream: buffers the selected stream
class InputFileSelectStream : public Base
{
public:
  InputFileSelectStream();
  InputFileSelectStream(const std::string &filename, AVMediaType type, int index = 0);
  ~InputFileSelectStream();

  void openFile(const std::string &filename, AVMediaType type, int index = 0);

  AVFrame *read_next_frame(const bool block = true);
  AVRational getFrameSAR(AVFrame *frame);
  double getFrameTimeStamp(const AVFrame *frame);

  bool eof();

  double getDuration() const;
  std::string getFilePath() const;
  int getBitsPerPixel() const;
  double getFrameRate() const;
  int getHeight() const;
  int getWidth() const;
  std::string getVideoPixelFormat() const;
  std::string getVideoCodecName() const;
  std::string getVideoCodecDesc() const;
  double getPTS() const;
  uint64_t getNumberOfFrames() const;

  void setPTS(const double val);
  // int get_packet(AVPacket &pkt); // call this function to get next media data packet

  // void seek(const int64_t timestamp);

  // FFmpeg callbacks
  static enum AVPixelFormat get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts);
  static int get_buffer(AVCodecContext *s, AVFrame *frame, int flags);

private:
  AVFormatContext *fmt_ctx; // file format context
  int stream_index;         // overall index of the loaded stream
  AVStream *st;             // selected stream

  AVCodec *dec;            // decoder
  AVCodecContext *dec_ctx; // decoder context

  AVPacketBuffer raw_packets;       // encoded packets as read
  AVFramePtrBuffer decoded_frames;  // decoded media frames
  AVFramePtrBuffer filtered_frames; // filtred media frames

  bool kill_threads;                  // set to terminate worker threads
  bool suspend_threads;               // set to pause worker threads
  std::mutex suspend_mutex;           // mutex to suspend/resume threads
  std::condition_variable suspend_cv; // condition variable to suspend/resume threads

  std::thread read_thread; /* thread to read packets from file */
  int read_state;

  std::thread decode_thread; /* thread to decode encoded frames (only activated if data are encoded) */
  int decode_state;

  std::thread filter_thread; /* thread to filter frames  (only activated if filtering is requested) */
  int filter_state;

  int loop;         /* set number of times input stream should be looped */
  bool eof_reached; /* true if eof reached */

  uint64_t frames_decoded;
  uint64_t samples_decoded; // only for audio

  uint64_t pts;
  // bool accurate_seek;
  // bool eagain;           /* true if data was not ready during the last read attempt */

  // int64_t next_pts;
  // int64_t filter_in_rescale_delta_last;
  // int resample_sample_fmt;
  // int resample_channels;
  // uint64_t resample_channel_layout;
  // int resample_sample_rate;
  // int guess_layout_max; // default: INT_MAX
  // int64_t dts;
  // int64_t nb_samples; // number of input samples to filter graph

  /////////////////////////////////////////////

  void open_file(const std::string &filename);
  void select_stream(AVMediaType type, int index = 0);
  void init_stream();

  void init_thread(void);
  void free_thread(void);

  void read_thread_fcn(); // thread function to read input packet

  void decode_thread_fcn(); // thread function to decode input packet to raw data frame(s)
  int decode_frame(AVFrame *frame, bool &got_frame, const AVPacket *pkt);

  //   void filter_thread_fcn(); // thread function to filter ipnut packet

  // int decode_audio(AVPacket *pkt, bool &got_output, int eof);
  // int decode_video(AVPacket *pkt, bool &got_output, int eof);

  // void prepare_packet(AVPacket &pkt);
};
};
