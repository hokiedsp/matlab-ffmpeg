#pragma once

#include <queue>

#include "ffmpegAVFrameBufferInterfaces.h"
#include "ffmpegAVFrameQueue.h"
#include "ffmpegFormatInput.h"
#include "filter/ffmpegFilterGraph.h"

#include "syncpolicies.h"

// #include "mexClassHandler.h"
// #include "ffmpegPtrs.h"
// #include "ffmpegAvRedefine.h"
// #include "ffmpegAVFramePtrBuffer.h"
// #include "ffmpegFrameBuffers.h"

namespace ffmpeg
{

// single-thread media file reader
class Reader
{
  public:
  Reader(const std::string &url = "");
  ~Reader(); // may need to clean up filtergraphs

  bool isFileOpen() { return file.isFileOpen(); }
  bool EndOfFile() { return file.EndOfFile(); }

  void openFile(const std::string &url)
  {
    file.openFile(url);
    eof = false;
  }
  void closeFile() { file.closeFile(); }

  int addStream(const int wanted_stream_id, int related_stream_id = -1)
  {
    int id = file.getStreamId(wanted_stream_id, related_stream_id);
    add_stream(id);
    return id;
  }
  int addStream(const AVMediaType type, int related_stream_id = -1)
  {
    int id = file.getStreamId(type, related_stream_id);
    add_stream(id);
    return id;
  }
  int addStream(const std::string &spec, int related_stream_id = -1)
  {
    int id = file.getStreamId(spec, related_stream_id);
    add_stream(id);
    return id;
  }

  int addFilterGraph(const std::string &desc, const AVMediaType type = AVMEDIA_TYPE_UNKNOWN);

  void clearStreams()
  {
    bufs.clear();
    file.clearStreams();
  }

  InputStream &getStream(int stream_id, int related_stream_id = -1) { return file.getStream(stream_id, related_stream_id); }
  InputStream &getStream(AVMediaType type, int related_stream_id = -1) { return file.getStream(type, related_stream_id); }

  const InputStream &getStream(int stream_id, int related_stream_id = -1) const { return file.getStream(stream_id, related_stream_id); }
  const InputStream &getStream(AVMediaType type, int related_stream_id = -1) const { return file.getStream(type, related_stream_id); }

  bool getNextFrame(AVFrame *frame, const int stream_id, const bool getmore = true) { return get_frame(frame, file.getStreamId(stream_id), getmore); }
  bool getNextFrame(AVFrame *frame, const std::string &spec, const bool getmore = true) { return get_frame(frame, file.getStreamId(spec), getmore); }

  // int64_t getCurrentTimeStamp() const;
  // void setCurrentTimeStamp(const int64_t val, const bool exact_search = true);
  // AVRational getTimeBase() const;

  double getNextTimeStamp() const { return file.getCurrentTimeStamp(); }
  void setNextTimeStamp(const double val, const bool exact_search = true) { file.setCurrentTimeStamp(val, exact_search); }

  std::string getFilePath() const { return file.getFilePath(); }
  double getDuration() const { return file.getDuration(); }

  // AVRational getSAR() const;

  // int getBitsPerPixel() const;
  // uint64_t getNumberOfFrames() const;

  // const std::string &getFilterGraph() const;                                                          // stops
  // void setFilterGraph(const std::string &filter_desc, const AVPixelFormat pix_fmt = AV_PIX_FMT_NONE); // stops

  // const AVPixelFormat getPixelFormat() const;
  // const AVPixFmtDescriptor &getPixFmtDescriptor() const;
  // size_t getNbPlanar() const;
  // size_t getNbPixelComponents() const;

  // size_t getWidth() const;
  // size_t getHeight() const;
  // size_t getFrameSize() const;
  // size_t getCurrentFrameCount();

  private:
  typedef AVFrameQueue<NullMutex, NullConditionVariable<NullMutex>, NullUniqueLock<NullMutex>> AVFrameQueueST;

  void add_stream(const int stream_id);
  // reads next set of packets from file/stream and push the decoded frame to the stream's sink
  bool get_frame(AVFrame *frame, const int stream_id, const bool getmore);
  void create_filters(const std::string &descr = "", const AVPixelFormat pix_fmt = AV_PIX_FMT_NONE);
  void destroy_filters();

  InputFormat file;
  std::unordered_map<int, AVFrameQueueST> bufs; // output frame buffers (one for each active stream)

  bool eof; // true if last frame read was an EOF
  uint64_t pts;

  std::vector<filter::Graph *>filter_graphs; // filter graphs
  std::unordered_map<std::string, AVFrameQueueST> filter_bufs; // filter output frame buffers (one for each active stream)

};
} // namespace ffmpeg
