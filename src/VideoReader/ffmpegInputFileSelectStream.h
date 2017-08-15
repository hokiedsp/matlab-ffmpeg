#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>

extern "C" {
#include <libavformat/avformat.h>    // for AVFormatContext
#include <libavutil/rational.h>      // for AVRational
#include <libavutil/threadmessage.h> // for AVThreadMessageQueue
}

#include "ffmpegBase.h"
#include "ffmpegPtrs.h"
#include "ffmpegFifoBuffer.h"

namespace ffmpeg
{

class AVFrameBuffer : FifoBuffer<AVFrame*>
{

};

// InputFileSelectStream: buffers the selected stream
class InputFileSelectStream : public ffmpegBase
{
 public:
   InputFileSelectStream(const std::string &filename, AVMediaType type, int index = 0);
   ~InputFileSelectStream();

   int get_packet(AVPacket &pkt); // call this function to get next media data packet

   void seek(const int64_t timestamp);

 private:
   FormatCtxPtr ctx; // file format context
   int stream_index; // overall index of the loaded stream
   AVStream *st;     // selected stream

   AVCodec *dec;        // decoder
   CodecCtxPtr dec_ctx; // decoder context

   bool reading;
   AVPacketPtrBuffer raw_packets;    // encoded packets as read
   AVFramePtrBuffer decoded_frames;  // decoded media frames
   AVFramePtrBuffer filtered_frames; // filtred media frames
   
   std::thread read_thread; /* thread to read packets from file */
   int read_state;

   std::thread decode_thread; /* thread to decode encoded frames (only activated if data are encoded) */
   int decode_state;

   std::thread filter_thread; /* thread to filter frames  (only activated if filtering is requested) */
   int filter_state;

   bool accurate_seek;
   int loop;              /* set number of times input stream should be looped */
   int thread_queue_size; /* maximum number of queued packets */
   bool eagain;           /* true if data was not ready during the last read attempt */
   int eof_reached;       /* true if eof reached */

   uint64_t samples_decoded;
   uint64_t frames_decoded;
   int64_t next_pts;
   int64_t filter_in_rescale_delta_last;
   int resample_sample_fmt;
   int resample_channels;
   uint64_t resample_channel_layout;
   int resample_sample_rate;
   int guess_layout_max; // default: INT_MAX
int64_t dts;
   int64_t nb_samples; // number of input samples to filter graph
   
   /////////////////////////////////////////////

   void open_file(const std::string &filename);
   void select_stream(AVMediaType type, int index = 0);

   void init_thread(void);
   void free_thread(void);

   void read_thread_fcn(); // thread function to read input packet
   void decode_thread_fcn(); // thread function to decode input packet to raw data frame(s)
   //   void filter_thread_fcn(); // thread function to filter ipnut packet
   
   int decode_frame(AVFrame *frame, bool &got_frame, const AVPacket *pkt);
   int decode_audio(AVPacket *pkt, bool &got_output, int eof);
   int decode_video(AVPacket *pkt, bool &got_output, int eof);

   // void prepare_packet(AVPacket &pkt);
};
}
