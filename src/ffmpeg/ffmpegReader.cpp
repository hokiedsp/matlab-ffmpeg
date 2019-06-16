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

Reader::Reader(const std::string &url) : file(url), eof(true), filter_graph(nullptr) {}

Reader::~Reader() {} // may need to clean up filtergraphs

void Reader::add_stream(const int id)
{
  if (id == AVERROR_STREAM_NOT_FOUND) throw ffmpegException("Invalid Stream ID.");

  file.addStream(id, bufs[id]); // create buffer & add stream
}

int Reader::addStream(const std::string &spec, int related_stream_id)
{
  // if filter graph is defined, check its output link labels first
  if (filter_graph && filter_graph->isSink(spec))
  {
    filter_graph->assignSink(filter_outbufs[spec], spec);
    return -1;
  }

  // check the input stream
  int id = file.getStreamId(spec, related_stream_id);
  if (id == AVERROR_STREAM_NOT_FOUND || file.isStreamActive(id))
    throw ffmpegException("Invalid stream or output filter link label.");
  add_stream(id);
  return id;
}

bool Reader::get_frame(AVFrame *frame, AVFrameQueueST &buf, const bool getmore)
{
  // if reached eof, nothing to do
  if (eof) return true;

  // read the next frame
  if (getmore)
    while (buf.empty() && file.readNextPacket() && filter_graph && filter_graph->processFrame())
      ;
  else if (buf.empty())
    return true;

  // pop the new frame from the buffer if available; also update eof flag
  buf.pop(frame, eof);

  return eof;
}

bool Reader::readNextFrame(AVFrame *frame, const std::string &spec, const bool getmore)
{
  // if filter graph is defined, check its output link labels first
  AVFrameQueueST *buf;
  try
  {
    buf = &filter_outbufs.at(spec);
  }
  catch (...)
  {
    buf = &bufs.at(file.getStreamId(spec));
  }
  return get_frame(frame, *buf, getmore);
}

////////////////////

int Reader::addFilterGraph(const std::string &desc, const AVMediaType type)
{
  if (filter_graph) delete filter_graph;

  // create new filter graph
  filter::Graph *fg = new filter::Graph(desc);

  // Resolve filters' input sources (throws exception if invalid streams assigned)
  fg->parseSourceStreamSpecs(std::vector<ffmpeg::InputFormat *>({&file}));

  // for each input pad
  int stream_id;
  std::string pad_name;
  while ((pad_name = fg->getNextUnassignedSourceLink(nullptr, &stream_id, pad_name)).size())
  {
    auto &buf = filter_inbufs[pad_name];
    file.addStream(stream_id, buf);
    fg->assignSource(buf, pad_name);
  }

  filter_graph = fg;

  return 0;
}
