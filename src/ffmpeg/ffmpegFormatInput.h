#pragma once

// #include "ThreadBase.h"
#include "ffmpegBase.h"
// #include "ffmpegAvRedefine.h"
#include "ffmpegStreamInput.h"
#include "ffmpegStreamIterator.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
  // #include <libavutil/pixdesc.h>
}

namespace ffmpeg
{

class InputFormat : public Base
{
public:
  InputFormat(const std::string &filename = "");
  virtual ~InputFormat();

  bool isFileOpen();
  bool EndOfFile();

  void openFile(const std::string &filename);
  void closeFile();

  // setting input options
  void setPixelFormat(const AVPixelFormat pix_fmt, const std::string &spec = "");

  InputStream &addStream(const int wanted_stream_id, IAVFrameSinkBuffer &buf, int related_stream_id = -1);
  InputStream &addStream(const AVMediaType type, IAVFrameSinkBuffer &buf, int related_stream_id = -1);
  InputStream &addStream(const std::string &spec, IAVFrameSinkBuffer &buf, int related_stream_id = -1);
  void clearStreams();

  bool isStreamActive(int stream_id) const { return (fmt_ctx && streams.find(stream_id) != streams.end()); }

  InputStream &getStream(const int stream_id, const int related_stream_id = -1);
  InputStream &getStream(const AVMediaType type, const int related_stream_id = -1);
  InputStream &getStream(const std::string &spec, const int related_stream_id = -1);

  const InputStream &getStream(const int stream_id, const int related_stream_id = -1) const;
  const InputStream &getStream(const AVMediaType type, const int related_stream_id = -1) const;
  const InputStream &getStream(const std::string &spec, const int related_stream_id = -1) const;

  // iterators for active streams
  using stream_iterator = StreamIterator<InputStream>;
  using const_stream_iterator = StreamIterator<const InputStream, std::unordered_map<int, InputStream *>::const_iterator>;
  using reverse_iterator = std::reverse_iterator<stream_iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_stream_iterator>;

  stream_iterator getStreamBegin() { return stream_iterator(streams.begin()); }
  const_stream_iterator getStreamBegin() const { return const_stream_iterator(streams.begin()); }
  const_stream_iterator getStreamCBegin() const { return const_stream_iterator(streams.cbegin()); }
  stream_iterator getStreamEnd() { return stream_iterator(streams.begin()); }
  const_stream_iterator getStreamEnd() const { return const_stream_iterator(streams.end()); }
  const_stream_iterator getStreamCEnd() const { return const_stream_iterator(streams.cend()); }

  // reads next packet from file/stream and push the decoded frame to the stream's sink
  // returns null if eof; else pointer to the stream, which was contained in the packet
  InputStream *readNextPacket();

  std::string getFilePath() const;
  AVRational getTimeBase() const;
  int64_t getDurationTB() const;
  double getDuration() const { return getDurationTB() / (double)AV_TIME_BASE; }

  double getCurrentTimeStamp() const { return getCurrentTimeStampTB() / (double)AV_TIME_BASE; }
  void setCurrentTimeStamp(const double val, const bool exact_search = true) { setCurrentTimeStampTB(int64_t(val * AV_TIME_BASE), exact_search); }

  int64_t getCurrentTimeStampTB() const;
  void setCurrentTimeStampTB(const int64_t val, const bool exact_search = true);

  int getStreamId(const int stream_id, const int related_stream_id = -1) const;
  int getStreamId(const AVMediaType type, const int related_stream_id = -1) const;
  int getStreamId(const std::string &spec, const int related_stream_id = -1) const;

  AVMediaType getStreamType(const int stream_id) const { return (fmt_ctx && stream_id >= 0 && stream_id >= (int)fmt_ctx->nb_streams) ? fmt_ctx->streams[stream_id]->codecpar->codec_type : AVMEDIA_TYPE_UNKNOWN; }
  AVMediaType getStreamType(const std::string &spec) const { return getStreamType(getStreamId(spec)); }

  int getNumberOfStreams() const { return fmt_ctx ? fmt_ctx->nb_streams : 0; }
  size_t getNumberOfActiveStreams() const { return streams.size(); }

  // low-level functions

  AVStream *_get_stream(int stream_id, int related_stream_id = -1) { return (fmt_ctx && (stream_id < (int)fmt_ctx->nb_streams)) ? fmt_ctx->streams[stream_id] : nullptr; }

protected:
  // thread function: responsible to read packet and send it to ffmpeg decoder
  // void thread_fcn();

private:
  virtual InputStream &add_stream(const int id, IAVFrameSinkBuffer &buf);

  AVFormatContext *fmt_ctx;                       // FFmpeg format context
  std::unordered_map<int, InputStream *> streams; // media streams under decoding

  int64_t pts;
  AVPacket packet;
};

} // namespace ffmpeg
