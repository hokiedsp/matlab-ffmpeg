#pragma once

#include "../ffmpegBase.h"
#include "../ffmpegFormatInput.h"
#include "ffmpegFilterSinks.h"
#include "ffmpegFilterSources.h"

// #include "mexClassHandler.h"
// #include "ffmpegPtrs.h"
// #include "ffmpegAvRedefine.h"
// #include "ffmpegAVFramePtrBuffer.h"
// #include "ffmpegFrameBuffers.h"

extern "C"
{
// #include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
  // #include <libavutil/pixdesc.h>
}

#include <chrono>
#include <unordered_map>
#include <utility>
#include <vector>

typedef std::vector<std::string> string_vector;

using namespace std::chrono_literals;

namespace ffmpeg
{
namespace filter
{
class Graph : public ffmpeg::Base
{
  struct SourceInfo;
  struct SinkInfo;

  public:
  Graph(const std::string &filtdesc = "");
  Graph(const Graph &src) = delete;               // non-copy constractible
  Graph(Graph &&src) { *this = std::move(src); }; // move ctor
  ~Graph();

  Graph &operator=(const Graph &src) = delete; // non-copyable
  Graph &operator=(Graph &&src)
  {
    graph = src.graph;
    src.graph = nullptr;
    graph_desc = std::move(src.graph_desc);
    inputs = std::move(src.inputs);
    outputs = std::move(src.outputs);
    inmon_status = src.inmon_status; // 0:no monitoring; 1:monitor; <0 quit

    return *this;
  }

  /**
   * \brief Destroys the current AVFilterGraph and release all the resources
   *
   * destroy() frees \ref graph
   */
  void clear();

  /**
   * \briefs Destroy the current AVFilterGraph
   *
   * purge() frees \ref graph using avfilter_graph_free() and purges all
   * invalidated AVFilter pointers from \ref inputs and \ref outputs maps. It
   * keeps 'filter' and 'buf' fields of \ref inputs and \ref outputs maps.
   *
   * Use \ref clear() to release all the resources associated with the current
   * filter graph
   *
   * When the associated AVFilterGraph is destroyed, \ref context becomes
   * invalid. ffmpeg::filter::Graph calls this function to make its filters
   * deassociate invalidated AVFilterContexts.
   */
  virtual void purge();

  /**
   * \brief Parse new filter graph
   *
   * \note Step 1 in building a new filter graph
   * \note Not thread-safe. Must be called while the object's thread is paused
   *
   * \param[in] desc Descriptive string of a new filter graph as done in
   * standard ffmpeg binary
   */
  void parse(const std::string &desc);

  /**
   * \brief Parse and match unassigned filter input names to the input streams
   * of the given input files.
   *
   * \param[in] fmts   Vector of pointers to the input file formats (all
   * pointers must be valid.)
   *
   * \throws Exception if the stream has already been taken
   */
  void parseSourceStreamSpecs(const std::vector<InputFormat *> fmts);

  /**
   * \brief returns the name, its expected file and stream IDs (if assigned and
   * requested) of filter input link without buffer.
   *
   * \param[in] spec      Input link name. If empty string, returns the
   * first unassigned input link.
   * \param[out] file_id   InputFormat index in the InputFormat vector provided
   * in parseSourceStreamSpecs() call
   * \param[out] stream_id Stream index of the
   * chosen InputFormat
   *
   * \returns the label of the next input link, which is yet has input AVFrame
   * buffer assigned to it. If all the links have been configured, returns
   * empty.
   *
   * \note file_id & stream_id are -1 unless parseSourceStreamSpecs() was called
   * beforehand
   */
  bool getSourceLink(const std::string &spec, int *file_id = nullptr,
                     int *stream_id = nullptr);

  /**
   * \brief returns the name, its expected file and stream IDs (if assigned and
   * requested) of filter input link without buffer.
   *
   * \param[out] file_id   InputFormat index in the InputFormat vector provided
   * in parseSourceStreamSpecs() call \param[out] stream_id Stream index of the
   * chosen InputFormat \param[in]  last      Input link name returned by the
   * last call of getNextUnassignedSourceLink(). If empty string, returns the
   * first unassigned input link.
   *
   * \returns the label of the next input link, which is yet has input AVFrame
   * buffer assigned to it. If all the links have been configured, returns
   * empty.
   *
   * \note file_id & stream_id are -1 unless parseSourceStreamSpecs() was called
   * beforehand
   */
  std::string getNextUnassignedSourceLink(int *file_id = nullptr,
                                          int *stream_id = nullptr,
                                          const std::string &last = "");

  /**
   * \brief Look up the source link label given file & stream ids as specified
   *        by parseSourceStreamSpecs() call
   *
   * \param[in] file_id   Index of the source file
   * \param[in] stream_id Index of the stream of the source file
   * \returns the label name of the matched filter source or empty string if
   *          none found
   */
  std::string findSourceLink(int file_id, int stream_id);

  /**
   * \brief   Get the name of the next unassigned filter sink
   *
   * \param[in] last Pass in the last name returned to go to the next
   * \param[in] type Specify to limit search to a particular media type
   *
   * \returns the name of the next unassigned filter sink. Returns empty if all
   * have been assigned.
   */
  std::string
  getNextUnassignedSink(const std::string &last = "",
                        const AVMediaType type = AVMEDIA_TYPE_UNKNOWN);

  /**
   * \brief Assign a source buffer to a parsed filtergraph
   *
   * assignSource() links the filter graph input with the given label \ref name
   * to the given AVFrame source buffer \ref buf. This function must follow a
   * successful \ref void parse(const std::string&) call and must be called
   * repeatedly to all of the utilized filter graph inputs. Use \ref
   * string_vector getInputNames() const to retrieve all the names of inputs.
   *
   * \note Step 2/3 in building a new filter graph
   * \note Not thread-safe. Must be called while the object's thread is paused
   *
   * \param[in] buf  Refererence to an AVFrame buffer object, from which the
   * input frames are drawn from \param[in] name Name of the input label on the
   * filer graph. Default is "in", which is used for a single-input graph with
   * unnamed input. \returns the reference of the created Source filter object
   */
  SourceBase &assignSource(IAVFrameSourceBuffer &buf,
                           const std::string &name = "in");

  /**
   * \brief Assign a sink buffer to a parsed filtergraph
   *
   * assignSink() links the filter graph output with the given label \ref name
   * to the given AVFrame sink buffer \ref buf. This function must follow a
   * successful \ref void parse(const std::string&) call, and must be called
   * repeatedly, once for each of the utilized filter graph outputs. Use \ref
   * string_vector getOutputNames() const to retrieve all the names of outputs.
   *
   * \note Step 2/3 in building a new filter graph
   * \note Not thread-safe. Must be called while the object's thread is paused
   *
   * \param[in] buf  Reference to an AVFrame buffer object, to which the output
   *                 frames are queued to \param[in] name Name of the output
   *                 label on the filer graph. Default is "out", which is used
   *                 for a single-output graph with unnamed input.
   *
   * \returns the reference of the created Source filter object
   */
  SinkBase &assignSink(IAVFrameSinkBuffer &buf,
                       const std::string &name = "out");

  /**
   * \brief Finalize the preparation of a new filtergraph
   *
   * finalizeGraph instantiates all the endpoint filter elements (buffer,
   * buffersink, abuffer, or abuffersink) as assigned by user.
   *
   * \note Lst step in building a new filter graph
   * \note Not thread-safe. Must be called while the object's thread is paused
   *
   */
  void configure();

  /**
   * \brief Returns true if the filter graph is ready to run
   *
   * \returns true if filter graph is ready to run
   */
  bool ready();

  /**
   * \brief Update the frame data
   *
   * finalizeGraph instantiates all the endpoint filter elements (buffer,
   * buffersink, abuffer, or abuffersink) as assigned by user.
   *
   * \note Lst step in building a new filter graph
   * \note Not thread-safe. Must be called while the object's thread is paused
   *
   */
  void updateParameters();

  /**
   * \brief Reset filter graph state / flush internal buffers
   *
   * flush() resets the internal state / buffers of the filter graph. It should
   * be called when seeking or when switching to receivign AVFrames with a
   * different parameters.
   *
   * flush() effectively rebuilds the filter as FFmpeg API currently does not
   * provide a way to flush an existing AVFilterGraph. Internal function may
   * change in the future when/if FFmpeg releases a "flush" function for an
   * AVFilterGraph.
   *
   * \note Not thread-safe. Must be called while the object's thread is paused
   */
  void flush();

  /**
   * \brief Execute the filter graph for one frame
   *
   * processFrame() executes one filtering transaction without the threaded
   * portion of ffmpeg::filter::Graph class. The input/source AVFrame buffers
   * linked to the filter graph must be pre-populated. If no AVFrame could be
   * retrieved, the function throws an exception.
   *
   * On the other end, the output AVFrame buffers should have enough capacity
   * so that all the filtered output AVFrame could be captured. If an output
   * buffer remains full after \var rel_time milliseconds, the filtered AVFrame
   * will be dropped.
   *
   * \returns total number of frames filter output.
   * \returns negative value if no input frame is available
   *
   * \throws Exception if fails to retrieve any input AVFrame from input
   * source buffers. \throws Exception if input buffer returns an error
   * during frame retrieval. \throws Exception if output buffer returns an
   * error during frame retrieval.
   */
  int processFrame();

  /**
   * \brief Returns true if all of its sources reached eof
   */
  bool EndOfFile()
  {
    return std::all_of(inputs.begin(), inputs.end(),
                       [](auto &in) { return in.second.filter->EndOfFile(); });
  }

  AVFilterGraph *getAVFilterGraph() const { return graph; }

  int insert_filter(AVFilterContext *&last_filter, int &pad_idx,
                    const std::string &filter_name, const std::string &args);

  std::string getFilterGraphDesc() const { return graph_desc; }
  string_vector getInputNames() const { return Graph::get_names(inputs); }
  string_vector getOutputNames() const { return Graph::get_names(outputs); }

  IAVFrameSourceBuffer *getInputBuffer(std::string name = "")
  {
    if (name.empty())
      return inputs.begin()->second.buf;
    else
      return inputs.at(name).buf;
  }
  IAVFrameSinkBuffer *getOutputBuffer(std::string name = "")
  {
    if (name.empty())
      return outputs.begin()->second.buf;
    else
      return outputs.at(name).buf;
  }

  template <typename EP, typename EPInfo> class EPIterator
  {
public:
    typedef typename std::unordered_map<std::string, EPInfo>::iterator
        bidir_iterator;
    typedef typename std::string key_type;
    typedef typename EP *mapped_type;
    typedef typename std::pair<key_type, mapped_type> value_type;

    EPIterator(bidir_iterator &map_it) : map_iter(map_it) {}
    bool operator==(const EPIterator &i) { return i.map_iter == map_iter; }
    bool operator!=(const EPIterator &i) { return i.map_iter != map_iter; }
    EPIterator &operator++()
    {
      ++map_iter;
      return *this;
    }
    EPIterator &operator--()
    {
      --map_iter;
      return *this;
    }
    value_type operator*() const
    {
      return std::make_pair(map_iter->first, map_iter->second.filter);
    }

private:
    typename std::unordered_map<std::string, EPInfo>::iterator map_iter;
  };

  EPIterator<SourceBase, SourceInfo> beginInputFilter()
  {
    return EPIterator<SourceBase, SourceInfo>(inputs.begin());
  }

  EPIterator<SourceBase, SourceInfo> endInputFilter()
  {
    return EPIterator<SourceBase, SourceInfo>(inputs.end());
  }

  EPIterator<SinkBase, SinkInfo> beginOutputFilter()
  {
    return EPIterator<SinkBase, SinkInfo>(outputs.begin());
  }

  EPIterator<SinkBase, SinkInfo> endOutputFilter()
  {
    return EPIterator<SinkBase, SinkInfo>(outputs.end());
  }

  bool isSimple() const { return inputs.size() == 1 && outputs.size() == 1; }

  bool isSource(const std::string &name)
  {
    return inputs.find(name) != inputs.end();
  }

  SourceBase &getSource(const std::string &name)
  {
    return *inputs.at(name).filter;
  }

  bool isSink(const std::string &name)
  {
    return outputs.find(name) != outputs.end();
  }

  SinkBase &getSink(const std::string &name)
  {
    return *outputs.at(name).filter;
  }

  // avfilter_graph_send_command, avfilter_graph_queue_command
  protected:
  template <typename EP, typename VEP, typename AEP, typename BUFF>
  void assign_endpoint(EP *&ep, AVMediaType type, BUFF &buf)
  {
    // if already defined, destruct existing
    if (ep) delete ep;

    // create new filter
    switch (type)
    {
    case AVMEDIA_TYPE_VIDEO:
      ep = new VEP(*this, buf);
      break;
    case AVMEDIA_TYPE_AUDIO: ep = new AEP(*this, buf); break;
    default:
      ep = NULL;
      throw Exception(
          "Only video and audio filters are supported at this time.");
    }
    if (!ep)
      throw Exception("[ffmpeg::filter::Graph::assign_endpoint] Failed "
                      "to allocate a new filter endpoint.");
  }

  private:
  AVFilterGraph *graph;
  std::string graph_desc;

  struct ConnectTo
  {
    AVFilterContext *other;
    int otherpad;
  };
  typedef std::vector<ConnectTo> ConnectionList;

  struct SourceInfo
  {
    AVMediaType type;
    int file_id;
    int stream_id;
    IAVFrameSourceBuffer *buf;
    SourceBase *filter;
    ConnectionList conns;
  };
  struct SinkInfo
  {
    AVMediaType type;
    int file_id;
    int stream_id;
    IAVFrameSinkBuffer *buf;
    SinkBase *filter;
    ConnectTo conn;
  };
  std::unordered_map<std::string, SourceInfo> inputs;
  std::unordered_map<std::string, SinkInfo> outputs;

  int inmon_status; // 0:no monitoring; 1:monitor; <0 quit

  template <typename InfoMap> static string_vector get_names(const InfoMap &map)
  {
    string_vector names;
    for (auto p = map.begin(); p != map.end(); ++p)
      names.emplace_back(p->first);
    return names;
  }

  void parse_sources(AVFilterInOut *ins);
  void parse_sinks(AVFilterInOut *outs);

  void connect_nullsources();
  void connect_nullsinks();

  void use_src_splitter(SourceBase *src, const ConnectionList &conns);
};
} // namespace filter
} // namespace ffmpeg
