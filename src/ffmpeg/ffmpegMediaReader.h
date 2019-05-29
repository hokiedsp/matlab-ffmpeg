#pragma once

#include "ThreadBase.h"
#include "ffmpegBase.h"
// #include "ffmpegAvRedefine.h"
#include "ffmpegStreamInput.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
// #include <libavutil/pixdesc.h>
}

#include <vector>

namespace ffmpeg
{

class MediaReader : public Base, protected ThreadBase
{
public:
  MediaReader(const std::string &filename = "", const AVMediaType type = AVMEDIA_TYPE_UNKNOWN);
  virtual ~MediaReader();

  bool isFileOpen();
  bool EndOfFile();

  void openFile(const std::string &filename, const AVMediaType type = AVMEDIA_TYPE_UNKNOWN);
  void closeFile();

  int addStream(int wanted_stream_id, int related_stream_id = -1);
  int addStream(AVMediaType type, int related_stream_id = -1);
  void addAllStreams();
  void clearStreams();

  void readNextPacket();
   
  int64_t getCurrentTimeStamp() const;
  void setCurrentTimeStamp(const int64_t val, const bool exact_search = true);

  std::string getFilePath() const;
  AVRational getTimeBase() const;
  int64_t getDuration() const;

protected:  
  // thread function: responsible to read packet and send it to ffmpeg decoder
  void thread_fcn();

private:
  virtual void add_stream(const int id);

  AVFormatContext *fmt_ctx;           // FFmpeg format context
  std::vector<InputStream*> streams;  // media streams under decoding
  
  int64_t pts;
};
}
