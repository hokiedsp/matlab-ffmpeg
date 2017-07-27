#pragma once

#include <thread>

extern "C" {
#include <libavformat/avformat.h>    // for AVFormatContext
#include <libavutil/rational.h>      // for AVRational
#include <libavutil/threadmessage.h> // for AVThreadMessageQueue
}

#include "ffmpegBase.h"
// #include "ffmpegOptionsContextInput.h"
#include "ffmpegInputStream.h"
#include "ffmpegPtrs.h"

namespace ffmpeg
{
class InputFileSelectStream : public ffmpegBase
{
public:
  FormatCtxPtr ctx;
  int stream_index;
  InputStream stream; // selected stream

  InputFile(const std::string &filename, AVMediaType type, int index = 0);
  ~InputFile();

  void select_stream(AVMediaType type, int index = 0);
  void seek(const int64_t timestamp);

  int get_packet(AVPacket &pkt);
  void prepare_packet(AVPacket &pkt);

  void init_thread(void);
  void free_thread(void);

protected:
  bool accurate_seek;
  int loop;              /* set number of times input stream should be looped */
  int thread_queue_size; /* maximum number of queued packets */
  bool eagain;           /* true if last read attempt returned EAGAIN */
  int eof_reached;       /* true if eof reached */

private:
  AVThreadMessageQueue *in_thread_queue;
  std::thread thread; /* thread reading from this file */
  bool non_blocking;  /* reading packets from the thread should not block */
  bool joined;        /* the thread has been joined */


  void input_thread();
};

typedef std::vector<InputFile> InputFiles;
}
