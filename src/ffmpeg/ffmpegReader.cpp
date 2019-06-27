#include "ffmpegReader.h"
#include "ffmpegAvRedefine.h"
#include "ffmpegException.h"
#include "ffmpegPtrs.h"

extern "C"
{
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
}

using namespace ffmpeg;

////////////////////////////////////////////////////////////////////////////////////////////////

Reader::Reader(const std::string &url)
    : file(url), active(false), filter_graph(nullptr)
{
}

Reader::~Reader() {} // may need to clean up filtergraphs

bool Reader::EndOfFile()
{
  return file.endOfFile() &&
         std::all_of(bufs.begin(), bufs.end(),
                     [](auto buf) { return buf.second.empty(); }) &&
         std::all_of(filter_outbufs.begin(), filter_outbufs.end(),
                     [](auto buf) { return buf.second.empty(); });
}

bool Reader::EndOfStream(const std::string &spec)
{
  if (!file.endOfFile()) return false;
  return get_buf(spec).empty();
}

IAVFrameSource &Reader::getStream(std::string spec, int related_stream_id)
{
  // if filter graph is defined, check its output link labels first
  if (filter_graph && filter_graph->isSink(spec))
    return filter_graph->getSink(spec);

  // check the input stream
  return file.getStream(spec, related_stream_id);
}

int Reader::addStream(const std::string &spec, int related_stream_id)
{
  if (active) Exception("Cannot add stream as the reader is already active.");

  // if filter graph is defined, check its output link labels first
  if (filter_graph && filter_graph->isSink(spec))
  {
    filter_graph->assignSink(filter_outbufs[spec], spec);
    return -1;
  }

  // check the input stream
  int id = file.getStreamId(spec, related_stream_id);
  if (id == AVERROR_STREAM_NOT_FOUND || file.isStreamActive(id))
    throw Exception("Invalid stream or output filter link label.");
  return add_stream(id);
}

Reader::AVFrameQueueST &Reader::read_next_packet()
{
  auto stream = file.readNextPacket();
  if (filter_graph) filter_graph->processFrame();
  return static_cast<AVFrameQueueST &>(stream->getSinkBuffer());
}

bool Reader::get_frame(AVFrameQueueST &buf)
{
  // read file until the target stream is reached
  while (buf.empty() && file.readNextPacket() && filter_graph &&
         filter_graph->processFrame())
    ;
  return buf.eof();
}

bool Reader::get_frame(AVFrame *frame, AVFrameQueueST &buf, const bool getmore)
{
  // if reached eof, nothing to do
  if (EndOfFile()) return true;

  // read the next frame (read multile packets until the target buffer is
  // filled)
  if ((getmore && get_frame(buf)) || buf.empty()) return true;

  // pop the new frame from the buffer if available; also update eof flag
  bool eof;
  buf.pop(frame, eof);

  return eof;
}

Reader::AVFrameQueueST &Reader::get_buf(const std::string &spec)
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

void Reader::flush()
{
  if (!active) return;
  for (auto buf : bufs) buf.second.clear();
  if (filter_graph)
  {
    for (auto buf : filter_inbufs) buf.second.clear();
    for (auto buf : filter_outbufs) buf.second.clear();
    filter_graph->flush();
  }
}

bool Reader::readNextFrame(AVFrame *frame, const std::string &spec,
                           const bool getmore)
{
  if (!active) throw Exception("Activate before read a frame.");
  // if filter graph is defined, check its output link labels first
  return get_frame(frame, get_buf(spec), getmore);
}

////////////////////

void Reader::setPixelFormat(const AVPixelFormat pix_fmt,
                            const std::string &spec)
{
  if (active)
    Exception("Cannot set pixel format as the reader is already active.");

  if (filter_graph) // if using filters, set filter
  {
    try
    {
      filter_graph->setPixelFormat(pix_fmt, spec);
      if (spec.empty()) file.setPixelFormat(pix_fmt, spec);
    }
    catch (const InvalidStreamSpecifier &)
    {
      // reaches only if spec is not found in the filter_graph
      file.setPixelFormat(pix_fmt, spec);
    }
  }
  else
  {
    file.setPixelFormat(pix_fmt, spec);
  }
}

int Reader::setFilterGraph(const std::string &desc)
{
  if (active)
    Exception("Cannot set filter graph as the reader is already active.");

  if (filter_graph) delete filter_graph;

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

std::string Reader::getNextInactiveStream(const std::string &last,
                                          const AVMediaType type,
                                          const int stream_sel)
{
  std::string spec;
  if (filter_graph &&
      (spec = filter_graph->getNextUnassignedSink(last, type)).size())
    return spec;
  if (stream_sel > 0) return "";

  int id = file.getStreamId(spec);
  return (id != AVERROR_STREAM_NOT_FOUND)
             ? std::to_string(file.getNextInactiveStream(id))
             : "";
}

void Reader::activate()
{
  if (active) return;

  if (file.ready()) throw Exception("Reader is not ready.");

  if (filter_graph)
  {
    // connect unused links to nullsrc/nullsink
    // then initializes the filter graph
    filter_graph->configure();
  }

  // read frames until all the buffers have at least one frame
  while (!file.EndOfFile() &&
         std::any_of(bufs.begin(), bufs.end(),
                     [](auto buf) { return buf.second.empty(); }))
  {
    file.readNextPacket();
    if (filter_graph) filter_graph->processFrame();
  }

  active = true;
}
