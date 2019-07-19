#pragma once

#include <string>
extern "C"

{
#include <libavutil/frame.h> // for AVFrame
}

#include "ffmpegAVFrameQueue.h"
#include "ffmpegFormatInput.h"
#include "ffmpegTimeUtil.h"

#include "ffmpegPostOp.h"
#include "filter/ffmpegFilterGraph.h"

#undef max
#undef min

namespace ffmpeg
{

enum StreamSource
{
  FilterSink = -1,
  Unspecified,
  Decoder
};

// single-thread media file reader
template <typename AVFrameQue> class Reader
{
  public:
  Reader(const std::string &url = "");
  Reader(const Reader &src) = delete;
  virtual ~Reader(); // may need to clean up filtergraphs

  Reader &operator=(const Reader &src) = delete;

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
   * \brief Add a filter graph to the reader
   *
   * \param[in] desc   Filter graph description to be parsed
   */
  int setFilterGraph(const std::string &desc);

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
  template <typename... Args>
  int addStream(const std::string &spec, int related_stream_id = -1,
                Args... args);
  template <typename... Args>
  int addStream(const int wanted_stream_id, int related_stream_id = -1,
                Args... args);
  template <typename... Args>
  int addStream(const AVMediaType type, int related_stream_id = -1,
                Args... args);

  InputStream &getStream(int stream_id, int related_stream_id = -1);
  InputStream &getStream(AVMediaType type, int related_stream_id = -1);
  IAVFrameSource &getStream(std::string spec, int related_stream_id = -1);

  const InputStream &getStream(int stream_id, int related_stream_id = -1) const;
  const InputStream &getStream(AVMediaType type,
                               int related_stream_id = -1) const;

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

  /**
   * \brief Adjust to the specified timestamp
   */
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

  protected:
  /**
   * \brief Read next packet
   * \returns the pointer to the stream which frame the packet contained. If
   *          EOF, returns null.
   */
  virtual void read_next_packet();

  // activate and adds buffer to an input stream by its id
  template <typename... Args> int add_stream(const int stream_id, Args... args);

  /**
   * \brief Helper function for seek(): Purge all the frames earlier than t
   */
  template <class Chrono_t> void purge_until(Chrono_t t);

  // input media file
  InputFormat file;

  bool active; // true to lock down the configuring interface, set by
               // activate() function

  std::unordered_map<int, AVFrameQue>
      bufs; // output frame buffers (one for each active stream)

  std::unordered_map<std::string, AVFrameQue>
      filter_outbufs; // filter output frame buffers (one for each active
                      // stream)

  private:
  AVFrameQue &get_buf(const std::string &spec);

  // reads next set of packets from file/stream and push the decoded frame
  // to the stream's sink
  bool get_frame(AVFrame *frame, AVFrameQue &buf, const bool getmore);

  /**
   * \brief Read file until buf has a frame
   */
  bool get_frame(AVFrameQue &buf);

  template <class Chrono_t> Chrono_t get_time_stamp(AVFrameQue &buf);

  bool at_end_of_stream(AVFrameQue &buf);

  filter::Graph *filter_graph; // filter graphs
  std::unordered_map<std::string, AVFrameQueueST>
      filter_inbufs; // filter output frame buffers (one for each active
                     // stream)
  // post-op objects for all streams
  std::unordered_map<AVFrameQue *, PostOpInterface *> postops;

  template <class PostOp, typename... Args>
  void emplace_postop(AVFrameQue &buf, Args... args);
};

// inline/template implementations

template <typename AVFrameQue>
Reader<AVFrameQue>::Reader(const std::string &url)
    : file(url), active(false), filter_graph(nullptr)
{
}

template <typename AVFrameQue> Reader<AVFrameQue>::~Reader()
{
  for (auto &postop : postops) delete postop.second;
}

template <typename AVFrameQue> bool Reader<AVFrameQue>::hasFrame()
{
  // buffer has a frame if not empty and at least one is a non-eof entry
  auto pred = [](auto &buf) {
    AVFrameQue &que = buf.second;
    return que.size() && !que.eof();
  };
  return active && std::any_of(bufs.begin(), bufs.end(), pred) &&
         std::any_of(filter_outbufs.begin(), filter_outbufs.end(), pred);
}

template <typename AVFrameQue>
bool Reader<AVFrameQue>::hasFrame(const std::string &spec)
{
  if (!active || file.atEndOfFile()) return false;
  auto &buf = get_buf(spec);
  return buf.size() && !buf.eof();
}

// returns true if all open streams have been exhausted
template <typename AVFrameQue> bool Reader<AVFrameQue>::atEndOfFile()
{
  if (!active) return false;

  // if all packets have already been read, EOF if all buffers are exhausted
  auto eof = [](auto &buf) {
    AVFrameQue &que = buf.second;
    return que.size() && que.eof();
  };
  if (std::all_of(bufs.begin(), bufs.end(), eof) &&
      std::all_of(filter_outbufs.begin(), filter_outbufs.end(), eof))
    return true;

  // if all buffers empty, read another packet and try again
  auto empty = [](auto &buf) { return buf.second.empty(); };
  if (std::all_of(bufs.begin(), bufs.end(), empty) &&
      std::all_of(filter_outbufs.begin(), filter_outbufs.end(), empty))
  {
    read_next_packet(); // guarantee to push something to a buffer
    return (std::all_of(bufs.begin(), bufs.end(), eof) &&
            std::all_of(filter_outbufs.begin(), filter_outbufs.end(), eof));
  }

  // otherwise (some buffers not empty and at least one is not eof)
  return false;
}

template <typename AVFrameQue>
IAVFrameSource &Reader<AVFrameQue>::getStream(std::string spec,
                                              int related_stream_id)
{
  // if filter graph is defined, check its output link labels first
  if (filter_graph && filter_graph->isSink(spec))
    return filter_graph->getSink(spec);

  // check the input stream
  return file.getStream(spec, related_stream_id);
}

template <typename AVFrameQue>
template <typename... Args>
int Reader<AVFrameQue>::addStream(const std::string &spec,
                                  int related_stream_id, Args... args)
{
  if (active) Exception("Cannot add stream as the reader is already active.");

  // if filter graph is defined, check its output link labels first
  if (filter_graph && filter_graph->isSink(spec))
  {
    auto buf = filter_outbufs.find(spec);
    if (buf == filter_outbufs.end())
      buf = filter_outbufs
                .emplace(std::piecewise_construct, std::forward_as_tuple(spec),
                         std::forward_as_tuple(args...))
                .first;
    filter_graph->assignSink(buf->second, spec);
    emplace_postop<PostOpPassThru>(buf->second);
    return -1;
  }

  // check the input stream
  int id = file.getStreamId(spec, related_stream_id);
  if (id == AVERROR_STREAM_NOT_FOUND || file.isStreamActive(id))
    throw InvalidStreamSpecifier(spec);
  return add_stream(id);
}

template <typename AVFrameQue> void Reader<AVFrameQue>::read_next_packet()
{
  file.readNextPacket();
  if (filter_graph) filter_graph->processFrame();
}

template <typename AVFrameQue>
bool Reader<AVFrameQue>::get_frame(AVFrameQue &buf)
{
  // read file until the target stream is reached
  while (buf.empty()) read_next_packet();
  return buf.eof();
}

template <typename AVFrameQue>
bool Reader<AVFrameQue>::get_frame(AVFrame *frame, AVFrameQue &buf,
                                   const bool getmore)
{
  // if reached eof, nothing to do
  if (atEndOfFile()) return true;

  // read the next frame (read multile packets until the target buffer is
  // filled)
  if ((getmore && get_frame(buf)) || buf.empty()) return true;

  // pop the new frame from the buffer if available; return the eof flag
  return postops.at(&buf)->filter(frame);
}

template <typename AVFrameQue>
AVFrameQue &Reader<AVFrameQue>::get_buf(const std::string &spec)
{
  try
  {
    return filter_outbufs.at(spec);
  }
  catch (...)
  {
    return bufs.at(file.getStreamId(spec));
  }
}

template <typename AVFrameQue> void Reader<AVFrameQue>::flush()
{
  if (!active) return;
  for (auto &buf : bufs) buf.second.clear();
  if (filter_graph)
  {
    for (auto &buf : filter_inbufs) buf.second.clear();
    for (auto &buf : filter_outbufs) buf.second.clear();
    filter_graph->flush();
  }
}

template <typename AVFrameQue>
bool Reader<AVFrameQue>::readNextFrame(AVFrame *frame, const std::string &spec,
                                       const bool getmore)
{
  if (!active) throw Exception("Activate before read a frame.");
  // if filter graph is defined, check its output link labels first
  return get_frame(frame, get_buf(spec), getmore);
}

////////////////////

template <typename AVFrameQue>
int Reader<AVFrameQue>::setFilterGraph(const std::string &desc)
{
  if (active)
    Exception("Cannot set filter graph as the reader is already active.");

  if (filter_graph)
  {
    delete filter_graph;
    filter_graph = nullptr;
  }

  // create new filter graph
  filter::Graph *fg = new filter::Graph(desc);

  // Resolve filters' input sources (throws exception if invalid streams
  // assigned)
  fg->parseSourceStreamSpecs(std::vector<ffmpeg::InputFormat *>({&file}));

  // Link all source filters to the input streams
  int stream_id;
  std::string pad_name;
  while ((pad_name =
              fg->getNextUnassignedSourceLink(nullptr, &stream_id, pad_name))
             .size())
  {
    auto &buf = filter_inbufs[pad_name];
    file.addStream(stream_id, buf);
    fg->assignSource(buf, pad_name);
  }

  filter_graph = fg;

  return 0;
}

template <typename AVFrameQue>
std::string
Reader<AVFrameQue>::getNextInactiveStream(const std::string &last,
                                          const AVMediaType type,
                                          const StreamSource stream_sel)
{
  std::string spec;
  if (stream_sel != StreamSource::Decoder && filter_graph &&
      (spec = filter_graph->getNextUnassignedSink(last, type)).size())
    return spec;
  if (stream_sel == StreamSource::FilterSink) return "";

  int id = file.getStreamId(spec);
  return (id != AVERROR_STREAM_NOT_FOUND)
             ? std::to_string(file.getNextInactiveStream(id))
             : "";
}

template <typename AVFrameQue> void Reader<AVFrameQue>::activate()
{
  if (active) return;

  if (!file.ready()) throw Exception("Reader is not ready.");

  if (filter_graph)
  {
    // connect unused links to nullsrc/nullsink
    // then initializes the filter graph
    filter_graph->configure();
  }

  // // read frames until all the buffers have at least one frame
  // while (!file.atEndOfFile() &&
  //        std::any_of(bufs.begin(), bufs.end(),
  //                    [](auto &buf) { return buf.second.empty(); }))
  //   read_next_packet();

  // if (filter_graph)
  // {
  //   // also make sure all the filter graph sink buffers are filled
  //   while (!file.atEndOfFile() &&
  //          std::any_of(filter_outbufs.begin(), filter_outbufs.end(),
  //                      [](auto &buf) { return buf.second.empty(); }))
  //     read_next_packet();

  // // then update media parameters of the sinks
  // for (auto &buf : filter_outbufs)
  // { dynamic_cast<filter::SinkBase &>(buf.second.getSrc()).sync(); } }

  active = true;
}

template <typename AVFrameQue>
inline void Reader<AVFrameQue>::openFile(const std::string &url)
{
  if (file.isFileOpen()) closeFile();
  file.openFile(url);
}

template <typename AVFrameQue> inline void Reader<AVFrameQue>::closeFile()
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

template <typename AVFrameQue>
inline bool Reader<AVFrameQue>::atEndOfStream(const std::string &spec)
{
  return at_end_of_stream(get_buf(spec)); // frame buffer for the stream
}

template <typename AVFrameQue>
inline bool Reader<AVFrameQue>::atEndOfStream(int stream_id)
{
  return at_end_of_stream(bufs.at(stream_id));
}

template <typename AVFrameQue>
inline bool Reader<AVFrameQue>::at_end_of_stream(AVFrameQue &buf)
{
  // if file is at eof, eos if spec's buffer is exhausted
  // else, read one more packet from file
  if (buf.size() && buf.eof()) { return true; }
  else
  {
    if (buf.empty()) read_next_packet();
    return buf.size() && buf.eof();
  }
}

template <typename AVFrameQue>
inline int Reader<AVFrameQue>::getStreamId(const int stream_id,
                                           const int related_stream_id) const
{
  int id = file.getStreamId(stream_id, related_stream_id);
  return (id == AVERROR_STREAM_NOT_FOUND ||
          (filter_graph && filter_graph->findSourceLink(0, id).size()))
             ? AVERROR_STREAM_NOT_FOUND
             : id;
}

template <typename AVFrameQue>
inline int Reader<AVFrameQue>::getStreamId(const AVMediaType type,
                                           const int related_stream_id) const
{
  int id = file.getStreamId(type, related_stream_id);
  return (id == AVERROR_STREAM_NOT_FOUND ||
          (filter_graph && filter_graph->findSourceLink(0, id).size()))
             ? AVERROR_STREAM_NOT_FOUND
             : id;
}

template <typename AVFrameQue>
inline int Reader<AVFrameQue>::getStreamId(const std::string &spec,
                                           const int related_stream_id) const
{
  return file.getStreamId(spec, related_stream_id);
}

template <typename AVFrameQue>
template <typename... Args>
inline int Reader<AVFrameQue>::addStream(const int wanted_stream_id,
                                         int related_stream_id, Args... args)
{
  int id = file.getStreamId(wanted_stream_id, related_stream_id);
  if (id < 0 || file.isStreamActive(id))
    throw InvalidStreamSpecifier(wanted_stream_id);
  return add_stream(id, args...);
}

template <typename AVFrameQue>
template <typename... Args>
inline int Reader<AVFrameQue>::addStream(const AVMediaType type,
                                         int related_stream_id, Args... args)
{
  int id = file.getStreamId(type, related_stream_id);
  if (id < 0 || file.isStreamActive(id)) throw InvalidStreamSpecifier(type);
  return add_stream(id, args...);
}

template <typename AVFrameQue>
inline InputStream &Reader<AVFrameQue>::getStream(int stream_id,
                                                  int related_stream_id)
{
  return file.getStream(stream_id, related_stream_id);
}
template <typename AVFrameQue>
inline InputStream &Reader<AVFrameQue>::getStream(AVMediaType type,
                                                  int related_stream_id)
{
  return file.getStream(type, related_stream_id);
}

template <typename AVFrameQue>
inline const InputStream &
Reader<AVFrameQue>::getStream(int stream_id, int related_stream_id) const
{
  return file.getStream(stream_id, related_stream_id);
}
template <typename AVFrameQue>
inline const InputStream &
Reader<AVFrameQue>::getStream(AVMediaType type, int related_stream_id) const
{
  return file.getStream(type, related_stream_id);
}

template <typename AVFrameQue>
inline bool Reader<AVFrameQue>::readNextFrame(AVFrame *frame,
                                              const int stream_id,
                                              const bool getmore)
{
  return get_frame(frame, bufs.at(file.getStreamId(stream_id)), getmore);
}

template <typename AVFrameQue>
template <class Chrono_t>
inline Chrono_t Reader<AVFrameQue>::getTimeStamp()
{
  if (!active) throw Exception("Activate before read a frame.");

  Chrono_t T = getDuration();

  // find the minimum timestamp of all active streams
  auto reduce_op = [T](const Chrono_t &t,
                       const std::pair<std::string, AVFrameQue> &buf) {
    return std::min(T, get_time_stamp<Chrono_t>(buf.second));
  };
  Chrono_t t =
      std::reduce(bufs.begin(), bufs.end(), Chrono_t::max(), reduce_op);
  return std::reduce(filter_outbufs.begin(), filter_outbufs.end(), t,
                     reduce_op);
}

template <typename AVFrameQue>
template <class Chrono_t>
inline Chrono_t Reader<AVFrameQue>::getTimeStamp(const std::string &spec)
{
  if (!active) throw Exception("Activate before read a frame.");
  return get_time_stamp<Chrono_t>(get_buf(spec));
}

template <typename AVFrameQue>
template <class Chrono_t>
inline Chrono_t Reader<AVFrameQue>::getTimeStamp(int stream_id)
{
  if (!active) throw Exception("Activate before read a frame.");
  return get_time_stamp<Chrono_t>(bufs.at(stream_id));
}

template <typename AVFrameQue>
template <class Chrono_t>
inline Chrono_t Reader<AVFrameQue>::get_time_stamp(AVFrameQue &buf)
{
  while (buf.empty()) read_next_packet();
  AVFrame *frame = buf.peekToPop();
  return (frame) ? get_timestamp<Chrono_t>(frame->best_effort_timestamp >= 0
                                               ? frame->best_effort_timestamp
                                               : frame->pts,
                                           buf.getSrc().getTimeBase())
                 : getDuration();
}

template <typename AVFrameQue>
template <class Chrono_t>
inline void Reader<AVFrameQue>::seek(const Chrono_t t0, const bool exact_search)
{
  // clear all the buffers
  flush();

  // seek (to near) the requested time
  file.seek<Chrono_t>(t0);
  if (atEndOfFile())
  {
    for (auto &buf : bufs) buf.second.push(nullptr);
    for (auto &buf : filter_outbufs) buf.second.push(nullptr);
  }
  else if (exact_search)
  {
    // throw away frames with timestamps younger than t0
    purge_until(t0);
  }
}

template <typename AVFrameQue>
template <class Chrono_t>
void Reader<AVFrameQue>::purge_until(Chrono_t t0)
{
  // purge all premature frames from all the output buffers. Read more
  // packets as needed to verify the time
  auto purge = [this, t0](AVFrameQue &que) {
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

template <typename AVFrameQue>
template <typename Chrono_t>
inline Chrono_t Reader<AVFrameQue>::getDuration() const
{
  return file.getDuration<Chrono_t>();
}

template <typename AVFrameQue>
template <class PostOp, typename... Args>
inline void Reader<AVFrameQue>::setPostOp(const std::string &spec, Args... args)
{
  emplace_postop<PostOp, Args...>(get_buf(spec), args...);
}

template <typename AVFrameQue>
template <class PostOp, typename... Args>
inline void Reader<AVFrameQue>::setPostOp(const int id, Args... args)
{
  emplace_postop<PostOp, Args...>(bufs.at(id), args...);
}

template <typename AVFrameQue>
template <typename... Args>
inline int Reader<AVFrameQue>::add_stream(const int stream_id, Args... args)
{
  // stream must not be already activated (i.e., already has a buffer assigned
  // to it)

  auto emplace_returned =
      bufs.emplace(std::piecewise_construct, std::forward_as_tuple(stream_id),
                   std::forward_as_tuple(args...));

  if (!emplace_returned.second)
    throw Exception("The specified stream has already been activated.");

  // create a new buffer
  auto &buf = emplace_returned.first->second;

  // activate the stream with the new buffer
  auto ret = file.addStream(stream_id, buf).getId();

  // default to just pass-through the output frame
  emplace_postop<PostOpPassThru>(buf);

  // return the stream id
  return ret;
}

template <typename AVFrameQue>
template <class PostOp, typename... Args>
inline void Reader<AVFrameQue>::emplace_postop(AVFrameQue &buf, Args... args)
{
  if (postops.count(&buf))
  {
    delete postops[&buf];
    postops[&buf] = nullptr;
  }
  postops[&buf] = new PostOp(buf, args...);
}

} // namespace ffmpeg
