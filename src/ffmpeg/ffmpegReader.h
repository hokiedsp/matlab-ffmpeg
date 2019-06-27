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

  /**
   * \brief Returns true if all streams have reached end-of-file
   */
  bool EndOfFile();

  bool EndOfStream(const std::string &spec);

  void openFile(const std::string &url)
  {
    if (file.isFileOpen()) closeFile();
    file.openFile(url);
  }
  void activate();
  void closeFile()
  {
    if (filter_graph)
    {
      delete (filter_graph);
      filter_graph = nullptr;
      filter_inbufs.clear();
      filter_outbufs.clear();
    }
    file.closeFile();
    active = false;
    bufs.clear();
  }

  /**
   * \brief set pixel format of video streams
   *
   * \param[in] pix_fmt New pixel format
   * \param[in] spec    Stream specifier string. Empty string (default) set the
   *                    pixel format for all the video streams
   */
  void setPixelFormat(const AVPixelFormat pix_fmt,
                      const std::string &spec = "");

  int getStreamId(const int stream_id, const int related_stream_id = -1) const
  {
    int id = file.getStreamId(stream_id, related_stream_id);
    return (id == AVERROR_STREAM_NOT_FOUND ||
            (filter_graph && filter_graph->findSourceLink(0, id).size()))
               ? AVERROR_STREAM_NOT_FOUND
               : id;
  }
  int getStreamId(const AVMediaType type,
                  const int related_stream_id = -1) const
  {
    int id = file.getStreamId(type, related_stream_id);
    return (id == AVERROR_STREAM_NOT_FOUND ||
            (filter_graph && filter_graph->findSourceLink(0, id).size()))
               ? AVERROR_STREAM_NOT_FOUND
               : id;
  }

  int getStreamId(const std::string &spec,
                  const int related_stream_id = -1) const
  {
    return file.getStreamId(spec, related_stream_id);
  }

  /**
   * \brief Activate a stream to read
   *
   * \param[in] spec              Input stream specifier or filter output link
   * label \param[in] related_stream_id Specifies related stream of the same
   * program (only relevant on input stream)
   */
  int addStream(const std::string &spec, int related_stream_id = -1);

  int addStream(const int wanted_stream_id, int related_stream_id = -1)
  {
    int id = file.getStreamId(wanted_stream_id, related_stream_id);
    return add_stream(id);
  }
  int addStream(const AVMediaType type, int related_stream_id = -1)
  {
    int id = file.getStreamId(type, related_stream_id);
    return add_stream(id);
  }

  /**
   * \brief Add a filter graph to the reader
   *
   * \param[in] desc   Filter graph description to be parsed
   */
  int setFilterGraph(const std::string &desc);

  void clearStreams()
  {
    bufs.clear();
    file.clearStreams();
  }

  InputStream &getStream(int stream_id, int related_stream_id = -1)
  {
    return file.getStream(stream_id, related_stream_id);
  }
  InputStream &getStream(AVMediaType type, int related_stream_id = -1)
  {
    return file.getStream(type, related_stream_id);
  }
  IAVFrameSource &getStream(std::string spec, int related_stream_id = -1);

  const InputStream &getStream(int stream_id, int related_stream_id = -1) const
  {
    return file.getStream(stream_id, related_stream_id);
  }
  const InputStream &getStream(AVMediaType type,
                               int related_stream_id = -1) const
  {
    return file.getStream(type, related_stream_id);
  }

  /**
   * \brief   Get the specifier of the next inactive output streams or filter
   * graph sink.
   *
   * \param[in] last Pass in the last name returned to go to the next
   * \param[in] type Specify to limit search to a particular media type
   * \param[in] stream_sel Specify to select from unfiltered (>0) or filtered
   *            (<0) or both types of inactive streams.
   *
   * \returns the name of the next unassigned stream specifier/output filter
   * label. Returns empty if all have been assigned.
   */
  std::string
  getNextInactiveStream(const std::string &last = "",
                        const AVMediaType type = AVMEDIA_TYPE_UNKNOWN,
                        const int stream_sel = 0);

  /**
   * \brief Empty all the buffers and filter graph states
   */
  void flush();

  /**
   * \brief   Get next frame of the specified stream
   */
  bool readNextFrame(AVFrame *frame, const int stream_id,
                     const bool getmore = true)
  {
    return get_frame(frame, bufs.at(file.getStreamId(stream_id)), getmore);
  }

  bool readNextFrame(AVFrame *frame, const std::string &spec,
                     const bool getmore = true);

  template <class Chrono_t>
  void seek(const Chrono_t val, const bool exact_search = true)
  {
    flush();
    file.seek<Chrono_t>(val);
    if (exact_search)
    {
      // read more packets/frames until
      auto buf = read_next_packet();
      AVFrame *frame = buf.peekToPop();
      while (get_timestamp(frame->best_effort_timestamp,
                           buf.getSrc().getTimeBase()) < val)
      {
        buf.pop();
        buf = read_next_packet();
        frame = buf.peekToPop();
      }
      // if filter graph is assigned, make sure all the filter sinks
      if (filter_graph)
      {
        auto check_bufs = [val](auto buf) {
          bool do_check;
          while ((do_check =
                      buf.size() &&
                      (buf.get_timestamp(buf.peekToPop()->best_effort_timestamp,
                                         buf.getSrc().getTimeBase()) < val)))
            buf.pop();
          return !buf.empty();
        };
      }
      auto while (std::all_of(filter_outbufs.begin(), filter_outbufs.end(), ))
      {
        read_next_packet();
      }
    }
  }
}

std::string
getFilePath() const
{
  return file.getFilePath();
}

template <typename Chrono_t = InputFormat::av_duration>
Chrono_t getDuration() const
{
  return file.getDuration<Chrono_t>();
}

private:
typedef AVFrameQueue<NullMutex, NullConditionVariable<NullMutex>,
                     NullUniqueLock<NullMutex>>
    AVFrameQueueST;

// activate and adds buffer to an input stream by its id
int add_stream(const int stream_id)
{
  return file.addStream(stream_id, bufs[stream_id]).getId();
}

AVFrameQueueST &get_buf(const std::string &spec);

// reads next set of packets from file/stream and push the decoded frame to
// the stream's sink
bool get_frame(AVFrame *frame, AVFrameQueueST &buf, const bool getmore);

/**
 * \brief Read file until buf has a frame
 */
bool get_frame(AVFrameQueueST &buf);

/**
 * \brief Read next packet and return the populated frame queue
 */
AVFrameQueueST &read_next_packet();

InputFormat file;
std::unordered_map<int, AVFrameQueueST>
    bufs; // output frame buffers (one for each active stream)

bool active; // true to lock down the configuring interface, set by activate()
             // function
std::chrono::nanoseconds pts;

filter::Graph *filter_graph; // filter graphs
std::unordered_map<std::string, AVFrameQueueST>
    filter_inbufs; // filter output frame buffers (one for each active stream)
std::unordered_map<std::string, AVFrameQueueST>
    filter_outbufs; // filter output frame buffers (one for each active
                    // stream)
};                  // namespace ffmpeg
} // namespace ffmpeg
