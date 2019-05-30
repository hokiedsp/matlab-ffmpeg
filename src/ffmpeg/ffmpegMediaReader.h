#pragma once

// #include "ThreadBase.h"
#include "ffmpegBase.h"
// #include "ffmpegAvRedefine.h"
#include "ffmpegStreamInput.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
  // #include <libavutil/pixdesc.h>
}

#include <map>

namespace ffmpeg
{

class MediaReader : public Base //, protected ThreadBase
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

  InputStream &getStream(int stream_id);
  InputStream &getStream(AVMediaType type, int related_stream_id = -1);

  const InputStream &getStream(int stream_id) const;
  const InputStream &getStream(AVMediaType type, int related_stream_id = -1) const;

  // reads next packet from file/stream and push the decoded frame to the stream's sink
  void readNextPacket();

  int64_t getCurrentTimeStamp() const;
  void setCurrentTimeStamp(const int64_t val, const bool exact_search = true);

  std::string getFilePath() const;
  AVRational getTimeBase() const;
  int64_t getDuration() const;

protected:
  // thread function: responsible to read packet and send it to ffmpeg decoder
  // void thread_fcn();

private:
  virtual void add_stream(const int id);

  AVFormatContext *fmt_ctx;             // FFmpeg format context
  std::map<int, InputStream *> streams; // media streams under decoding

  int64_t pts;
  AVPacket packet;
};
} // namespace ffmpeg
