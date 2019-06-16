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
  void closeFile()
  {
    if (filter_graph) delete (filter_graph);
    file.closeFile();
    bufs.clear();
    filter_inbufs.clear();
    filter_outbufs.clear();
  }

  /**
   * \brief Activate a stream to read
   * 
   * \param[in] spec              Input stream specifier or filter output link label
   * \param[in] related_stream_id Specifies related stream of the same program 
   *                              (only relevant on input stream)
   */
  int addStream(const std::string &spec, int related_stream_id = -1);

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

  /**
   * \brief   Get next frame of the specified stream
   */
  bool readNextFrame(AVFrame *frame, const int stream_id, const bool getmore = true) { return get_frame(frame, bufs.at(file.getStreamId(stream_id)), getmore); }

  bool readNextFrame(AVFrame *frame, const std::string &spec, const bool getmore = true);

  double getNextTimeStamp() const { return file.getCurrentTimeStamp(); }
  void setNextTimeStamp(const double val, const bool exact_search = true) { file.setCurrentTimeStamp(val, exact_search); }

  std::string getFilePath() const { return file.getFilePath(); }
  double getDuration() const { return file.getDuration(); }

  private:
  typedef AVFrameQueue<NullMutex, NullConditionVariable<NullMutex>, NullUniqueLock<NullMutex>> AVFrameQueueST;

  // activate and adds buffer to an input stream by its id
  void add_stream(const int stream_id);
  // reads next set of packets from file/stream and push the decoded frame to the stream's sink
  bool get_frame(AVFrame *frame, AVFrameQueueST &buf, const bool getmore);

  InputFormat file;
  std::unordered_map<int, AVFrameQueueST> bufs; // output frame buffers (one for each active stream)

  bool eof; // true if last frame read was an EOF
  uint64_t pts;

  filter::Graph *filter_graph;                                    // filter graphs
  std::unordered_map<std::string, AVFrameQueueST> filter_inbufs;  // filter output frame buffers (one for each active stream)
  std::unordered_map<std::string, AVFrameQueueST> filter_outbufs; // filter output frame buffers (one for each active stream)
};
} // namespace ffmpeg
