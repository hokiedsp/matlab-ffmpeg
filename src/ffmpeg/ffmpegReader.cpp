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

Reader::Reader(const std::string &url) : file(url), eof(true) {}

Reader::~Reader() {} // may need to clean up filtergraphs

void Reader::add_stream(const int id)
{
  if (id == AVERROR_STREAM_NOT_FOUND) throw ffmpegException("Invalid Stream ID.");

  file.addStream(id, bufs[id]); // create buffer & add stream
}

bool Reader::get_frame(AVFrame *frame, const int stream_id, const bool getmore)
{
  // if reached eof, nothing to do
  if (eof) return true;

  // get the associated buffer for the stream
  AVFrameQueueST &buf = bufs.at(stream_id);

  // read the next frame
  if (getmore)
    while (buf.empty() && file.readNextPacket())
      ;
  else if (buf.empty())
    return true;

  // pop the new frame from the buffer if available; also update eof flag
  buf.pop(frame, eof);

  return eof;
}

////////////////////

int Reader::addFilterGraph(const std::string &desc, const AVMediaType type)
{
  // create new filter graph
  filter::Graph *fg = new filter::Graph(desc);

  // Resolve filters' input sources (throws exception if invalid streams assigned)
  fg->parseSourceStreamSpecs(std::vector<ffmpeg::InputFormat *>({&file}));

  // for each input pad
  int stream_id;
  std::string pad_name;
  while ((pad_name = fg->getNextUnassignedSourcePad(nullptr, &stream_id, pad_name)).size())
  {
    auto &buf = filter_bufs[pad_name];
    file.addStream(stream_id, buf);
    fg->assignSource(buf, pad_name);
  }

  filter_graphs.push_back(fg);

  return 0;
}

void Reader::destroy_filters()
{
}

void Reader::create_filters(const std::string &filter_description, const AVPixelFormat pix_fmt_rq)
{
}

// void Reader::copy_frame_ts(const AVFrame *frame)
// {
//   if (frame)
//   {
//     if (!firstframe)
//     {
//       // keep the first frame as the reference
//       std::unique_lock<std::mutex> firstframe_guard(firstframe_lock);
//       firstframe = av_frame_clone(frame);
//       firstframe_ready.notify_one();
//     }

//     // if seeking to a specified pts
//     if (buf_start_ts)
//     {
//       if (frame->best_effort_timestamp < buf_start_ts)
//         // {  av_log(NULL,AV_LOG_INFO,"ffmpeg::Reader::copy_frame_ts::dropping t=%d < %d\n",frame->best_effort_timestamp,buf_start_ts);
//         return;
//       else
//         buf_start_ts = 0;
//     }
//   }

//   std::unique_lock<std::mutex> buffer_guard(buffer_lock);
//   // if null, reached the end-of-file (or stopped by user command)
//   // copy frame to buffer; if buffer not ready, wait until it is
//   int ret = (buf) ? buf->copy_frame(frame, tb) : AVERROR(EAGAIN);
//   bool flush_frames;
//   while (!((flush_frames = filter_status == PAUSE_RQ) || killnow) && (ret == AVERROR(EAGAIN)))
//   {
//     buffer_ready.wait(buffer_guard);
//     if (killnow || flush_frames)
//       break;
//     ret = (buf) ? buf->copy_frame(frame, tb) : AVERROR(EAGAIN);
//   }

//   // if (!flush_frames)
//   //   av_log(NULL, AV_LOG_INFO, "ffmpeg::Reader::copy_frame_ts::buffering t=%d\n", frame ? frame->pts : -1);

//   // if (killnow || !ret) // skip only if buffer was not ready
//   buffer_ready.notify_one();
// }
