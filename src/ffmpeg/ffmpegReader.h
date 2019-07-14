#pragma once

#include <string>
extern "C"

{
#include <libavutil/frame.h> // for AVFrame
}

#include "ffmpegAVFrameQueue.h"
#include "ffmpegFormatInput.h"
#include "ffmpegTimeUtil.h"
#include "syncpolicies.h"

#include "ffmpegPostOp.h"
#include "filter/ffmpegFilterGraph.h"

#undef max
#undef min

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
   * \brief Returns true if there is any AVFrame in its output buffers
   */
  bool hasFrame();

  /**
   * \brief Returns true if there is any AVFrame in its output buffers
   */
  bool hasFrame(const std::string &spec);

  /**
   * \brief Returns true if all streams have reached end-of-file
   */
  bool atEndOfFile();

  bool atEndOfStream(const std::string &spec);
  bool atEndOfStream(int stream_id);

  /**
   * \brief Open a file at the given URL
   * \param[in] url
   * \throws if cannot open the specified URL
   * \throws if cannot retrieve stream info
   */
  void openFile(const std::string &url);
  void activate();
  void closeFile();

  /**
   * \brief set pixel format of video streams
   *
   * \param[in] pix_fmt New pixel format
   * \param[in] spec    Stream specifier string. Empty string (default) set the
   *                    pixel format for all the video streams
   */
  void setPixelFormat(const AVPixelFormat pix_fmt,
                      const std::string &spec = "");

  size_t getStreamCount() { return file.getNumberOfStreams(); }

  int getStreamId(const int stream_id, const int related_stream_id = -1) const;
  int getStreamId(const AVMediaType type,
                  const int related_stream_id = -1) const;
  int getStreamId(const std::string &spec,
                  const int related_stream_id = -1) const;

  /**
   * \brief Activate a stream to read
   *
   * \param[in] spec              Input stream specifier or filter
   *                              output link label
   * \param[in] related_stream_id Specifies related stream of the same
   *                              program (only relevant on input stream)
   * \returns the stream id if decoder output
   */
  int addStream(const std::string &spec, int related_stream_id = -1);

  int addStream(const int wanted_stream_id, int related_stream_id = -1);
  int addStream(const AVMediaType type, int related_stream_id = -1);

  /**
   * \brief Add a filter graph to the reader
   *
   * \param[in] desc   Filter graph description to be parsed
   */
  int setFilterGraph(const std::string &desc);

  void clearStreams();

  InputStream &getStream(int stream_id, int related_stream_id = -1);
  InputStream &getStream(AVMediaType type, int related_stream_id = -1);
  IAVFrameSource &getStream(std::string spec, int related_stream_id = -1);

  const InputStream &getStream(int stream_id, int related_stream_id = -1) const;
  const InputStream &getStream(AVMediaType type,
                               int related_stream_id = -1) const;

  enum StreamSource
  {
    FilterSink = -1,
    Unspecified,
    Decoder
  };

  /**
   * \brief   Get the specifier of the next inactive output streams or filter
   * graph sink.
   *
   * \param[in] last Pass in the last name returned to go to the next
   * \param[in] type Specify to limit search to a particular media type
   * \param[in] stream_sel Specify to stream source:
   *                          StreamSource::Decoder
   *                          StreamSource::FilterSink
   *                          StreamSource::Unspecified (default)
   *
   * \returns the name of the next unassigned stream specifier/output filter
   * label. Returns empty if all have been assigned.
   */
  std::string getNextInactiveStream(
      const std::string &last = "",
      const AVMediaType type = AVMEDIA_TYPE_UNKNOWN,
      const StreamSource stream_sel = StreamSource::Unspecified);

  /**
   * \brief Empty all the buffers and filter graph states
   */
  void flush();

  /**
   * \brief   Get next frame of the specified stream
   *
   * \param[out] frame     Pointer to a pre-allocated AVFrame object.
   * \param[in]  stream_id Media stream ID to read
   * \param[in]  getmore   (Optional) Set true to read more packets if no frame
   *                       is in buffer.
   * \returns true if failed to acquire the frame, either due to eof or empty
   *          buffer.
   */
  bool readNextFrame(AVFrame *frame, const int stream_id,
                     const bool getmore = true);

  bool readNextFrame(AVFrame *frame, const std::string &spec,
                     const bool getmore = true);

  /**
   * \brief Get the youngest time stamp in the queues
   */
  template <class Chrono_t> Chrono_t getTimeStamp();
  /**
   * \brief Get the youngest time stamp of the specified stream
   */
  template <class Chrono_t> Chrono_t getTimeStamp(const std::string &spec);
  template <class Chrono_t> Chrono_t getTimeStamp(int stream_id);

  template <class Chrono_t>
  void seek(const Chrono_t t0, const bool exact_search = true);

  std::string getFilePath() const { return file.getFilePath(); }

  template <typename Chrono_t = InputFormat::av_duration>
  Chrono_t getDuration() const;
  const AVDictionary *getMetadata() const { return file.getMetadata(); }

  /////////////////////////////////////////////////////////////////////////////
  /**
   * \brief Set post-filter object to retrieve the AVFrame
   *
   * \param[in] spec      Stream specifier (must be active)
   * \param[in] postfilt  Post-filter object
   */
  template <class PostOp, typename... Args>
  void setPostOp(const std::string &spec, Args... args);

  /**
   * \brief Set post-filter object to retrieve the AVFrame
   *
   * \param[in] id        Stream id (must be active)
   * \param[in] postfilt  Post-filter object (must stay valid)
   */
  template <class PostOp, typename... Args>
  void setPostOp(const int id, Args... args);

  private:
  typedef AVFrameQueue<NullMutex, NullConditionVariable<NullMutex>,
                       NullUniqueLock<NullMutex>>
      AVFrameQueueST;

  // activate and adds buffer to an input stream by its id
  int add_stream(const int stream_id);

  AVFrameQueueST &get_buf(const std::string &spec);

  // reads next set of packets from file/stream and push the decoded frame
  // to the stream's sink
  bool get_frame(AVFrame *frame, AVFrameQueueST &buf, const bool getmore);

  /**
   * \brief Read file until buf has a frame
   */
  bool get_frame(AVFrameQueueST &buf);

  template <class Chrono_t> Chrono_t get_time_stamp(AVFrameQueueST &buf);

  /**
   * \brief Read next packet
   * \returns the pointer to the stream which frame the packet contained. If
   *          EOF, returns null.
   */
  ffmpeg::InputStream *read_next_packet();

  InputFormat file;
  std::unordered_map<int, AVFrameQueueST>
      bufs; // output frame buffers (one for each active stream)

  bool active; // true to lock down the configuring interface, set by
               // activate() function
  std::chrono::nanoseconds pts;

  filter::Graph *filter_graph; // filter graphs
  std::unordered_map<std::string, AVFrameQueueST>
      filter_inbufs; // filter output frame buffers (one for each active
                     // stream)
  std::unordered_map<std::string, AVFrameQueueST>
      filter_outbufs; // filter output frame buffers (one for each active
                      // stream)

  // post-op objects for all streams
  std::unordered_map<AVFrameQueueST *, PostOpInterface *> postops;

  template <class PostOp, typename... Args>
  void emplace_postop(AVFrameQueueST &buf, Args... args);
};

// inline/template implementations

inline void Reader::openFile(const std::string &url)
{
  if (file.isFileOpen()) closeFile();
  file.openFile(url);
}

inline void Reader::closeFile()
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

inline int Reader::getStreamId(const int stream_id,
                               const int related_stream_id) const
{
  int id = file.getStreamId(stream_id, related_stream_id);
  return (id == AVERROR_STREAM_NOT_FOUND ||
          (filter_graph && filter_graph->findSourceLink(0, id).size()))
             ? AVERROR_STREAM_NOT_FOUND
             : id;
}

inline int Reader::getStreamId(const AVMediaType type,
                               const int related_stream_id) const
{
  int id = file.getStreamId(type, related_stream_id);
  return (id == AVERROR_STREAM_NOT_FOUND ||
          (filter_graph && filter_graph->findSourceLink(0, id).size()))
             ? AVERROR_STREAM_NOT_FOUND
             : id;
}

inline int Reader::getStreamId(const std::string &spec,
                               const int related_stream_id) const
{
  return file.getStreamId(spec, related_stream_id);
}

inline int Reader::addStream(const int wanted_stream_id, int related_stream_id)
{
  int id = file.getStreamId(wanted_stream_id, related_stream_id);
  if (id < 0 || file.isStreamActive(id))
    throw InvalidStreamSpecifier(wanted_stream_id);
  return add_stream(id);
}

inline int Reader::addStream(const AVMediaType type, int related_stream_id)
{
  int id = file.getStreamId(type, related_stream_id);
  if (id < 0 || file.isStreamActive(id)) throw InvalidStreamSpecifier(type);
  return add_stream(id);
}

inline void Reader::clearStreams()
{
  bufs.clear();
  file.clearStreams();
}

inline InputStream &Reader::getStream(int stream_id, int related_stream_id)
{
  return file.getStream(stream_id, related_stream_id);
}
inline InputStream &Reader::getStream(AVMediaType type, int related_stream_id)
{
  return file.getStream(type, related_stream_id);
}

inline const InputStream &Reader::getStream(int stream_id,
                                            int related_stream_id) const
{
  return file.getStream(stream_id, related_stream_id);
}
inline const InputStream &Reader::getStream(AVMediaType type,
                                            int related_stream_id) const
{
  return file.getStream(type, related_stream_id);
}

inline bool Reader::readNextFrame(AVFrame *frame, const int stream_id,
                                  const bool getmore)
{
  return get_frame(frame, bufs.at(file.getStreamId(stream_id)), getmore);
}

template <class Chrono_t> inline Chrono_t Reader::getTimeStamp()
{
  if (!active) throw Exception("Activate before read a frame.");

  Chrono_t T = getDuration();

  // get the timestamp of the next frame and return the smaller of it or the
  // smallest so far
  auto reduce_op = [T](const Chrono_t &t,
                       const std::pair<std::string, AVFrameQueueST> &buf) {
    auto &que = buf.second;
    if (que.empty()) return t; // no data

    AVFrame *frame = que.peekToPop();
    return std::min(T, (frame) ? get_timestamp(frame->best_effort_timestamp,
                                               que.getSrc().getTimeBase())
                               : T);
  };
  Chrono_t t =
      std::reduce(bufs.begin(), bufs.end(), Chrono_t::max(), reduce_op);
  t = std::reduce(filter_outbufs.begin(), filter_outbufs.end(), t, reduce_op);

  // if no frame avail (the initial value unchanged), read the next frame
  if (t == Chrono_t::max())
  {
    auto &st = read_next_packet();
    if (st)
    {
      auto &que = st->getSinkBuffer();
      t = get_timestamp(que.peekToPop()->best_effort_timestamp,
                        que.getSrc().getTimeBase());
    }
    else
    {
      t = T;
    }
  }
  return t;
}

template <class Chrono_t>
inline Chrono_t Reader::getTimeStamp(const std::string &spec)
{
  if (!active) throw Exception("Activate before read a frame.");
  return get_time_stamp<Chrono_t>(get_buf(spec));
}

template <class Chrono_t> inline Chrono_t Reader::getTimeStamp(int stream_id)
{
  if (!active) throw Exception("Activate before read a frame.");
  return get_time_stamp<Chrono_t>(bufs.at(stream_id));
}

template <class Chrono_t>
inline Chrono_t Reader::get_time_stamp(AVFrameQueueST &buf)
{
  while (buf.empty()) read_next_packet();
  AVFrame *frame = buf.peekToPop();
  return (frame) ? get_timestamp<Chrono_t>(frame->best_effort_timestamp >= 0
                                               ? frame->best_effort_timestamp
                                               : frame->pts,
                                           buf.getSrc().getTimeBase())
                 : getDuration();
}

template <class Chrono_t>
inline void Reader::seek(const Chrono_t t0, const bool exact_search)
{
  flush();
  file.seek<Chrono_t>(t0);
  if (atEndOfFile())
  {
    for (auto &buf : bufs) buf.second.push(nullptr);
    for (auto &buf : filter_outbufs) buf.second.push(nullptr);
  }
  else if (exact_search)
  {
    // purge all premature frames from all the output buffers. Read more
    // packets as needed to verify the time
    auto purge = [this, t0](AVFrameQueueST &que) {
      AVFrame *frame;
      while (que.empty() ||
             (frame = que.peekToPop()) &&
                 get_timestamp<Chrono_t>(frame->best_effort_timestamp,
                                         que.getSrc().getTimeBase()) < t0)
      {
        if (que.size())
          que.pop();
        else
          read_next_packet();
      }
    };
    for (auto &buf : bufs) purge(buf.second);
    for (auto &buf : filter_outbufs) purge(buf.second);
  }
}

template <typename Chrono_t> inline Chrono_t Reader::getDuration() const
{
  return file.getDuration<Chrono_t>();
}

template <class PostOp, typename... Args>
inline void Reader::setPostOp(const std::string &spec, Args... args)
{
  emplace_postop<PostOp, Args...>(get_buf(spec), args...);
}

template <class PostOp, typename... Args>
inline void Reader::setPostOp(const int id, Args... args)
{
  emplace_postop<PostOp, Args...>(bufs.at(id), args...);
}

inline int Reader::add_stream(const int stream_id)
{
  auto &buf = bufs[stream_id];
  auto ret = file.addStream(stream_id, buf).getId();
  emplace_postop<PostOpPassThru>(buf);
  return ret;
}

template <class PostOp, typename... Args>
inline void Reader::emplace_postop(AVFrameQueueST &buf, Args... args)
{
  if (postops.count(&buf))
  {
    delete postops[&buf];
    postops[&buf] = nullptr;
  }
  postops[&buf] = new PostOp(buf, args...);
}

} // namespace ffmpeg
