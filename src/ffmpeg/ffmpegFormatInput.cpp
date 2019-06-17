#include "ffmpegFormatInput.h"

#include "ffmpegAvRedefine.h"
#include "ffmpegException.h"
#include "ffmpegPtrs.h"

extern "C"
{
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
}

#include <algorithm>

using namespace ffmpeg;

InputFormat::InputFormat(const std::string &filename)
    : fmt_ctx(NULL), pts(AV_NOPTS_VALUE)
{
  // initialize to allow preemptive unreferencing
  if (!filename.empty()) openFile(filename);

  // initialize packet struct
  av_init_packet(&packet);
}

InputFormat::~InputFormat()
{
  // release whatever currently is in the packet holder
  av_packet_unref(&packet);

  // close the file
  closeFile();
}

////////////////////////////////////////////////////////////////////////////////////////////////

bool InputFormat::isFileOpen() { return fmt_ctx; }
bool InputFormat::EndOfFile() { return fmt_ctx && pts >= getDuration(); }

void InputFormat::openFile(const std::string &filename)
{
  int ret;

  if (fmt_ctx)
    throw ffmpegException("Another file already open. Close it first.");

  if ((ret = avformat_open_input(&fmt_ctx, filename.c_str(), NULL, NULL)) < 0)
    throw ffmpegException("Cannot open input file");

  if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
    throw ffmpegException("Cannot find stream information");

  // initially set to ignore all other streams (an InputStream object sets it to
  // AVDISCARD_NONE when it opens a stream)
  for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i)
    fmt_ctx->streams[i]->discard = AVDISCARD_ALL;
}

void InputFormat::closeFile()
{
  // nothing to do if file is not open
  if (!isFileOpen()) return;

  // close all the streams
  clearStreams();

  // close the file
  if (fmt_ctx) avformat_close_input(&fmt_ctx);
}

////////////////////////////////////////////////////////////////////////////////////////////////

void InputFormat::setPixelFormat(const AVPixelFormat pix_fmt,
                                 const std::string &spec)
{
  if (spec.size())
    dynamic_cast<InputVideoStream &>(getStream(spec)).setPixelFormat(pix_fmt);
  else if (pix_fmt != AV_PIX_FMT_NONE &&
           av_opt_set_pixel_fmt(fmt_ctx, "pix_fmt", pix_fmt, 0) < 0)
    throw ffmpegException("Invalid pixel format specified.");
}

////////////////////////////////////////////////////////////////////////////////////////////////

// int av_opt_set_video_rate(void *obj, const char *name, AVRational val, int
// search_flags);

////////////////////////////////////////////////////////////////////////////////////////////////

int InputFormat::getStreamId(const int wanted_stream_id,
                             const int related_stream_id) const
{
  int ret = AVERROR_STREAM_NOT_FOUND;
  if (!fmt_ctx) return ret; // if file not open, no stream avail

  int nb_streams = fmt_ctx->nb_streams;
  if (related_stream_id >= 0)
  {
    unsigned *program = NULL;
    AVProgram *p =
        av_find_program_from_stream(fmt_ctx, NULL, related_stream_id);
    if (p)
    {
      program = p->stream_index;
      nb_streams = p->nb_stream_indexes;
    }
    for (int i = 0; i < nb_streams; i++)
    {
      int real_stream_index = program[i];
      AVStream *st = fmt_ctx->streams[real_stream_index];
      if (real_stream_index != wanted_stream_id)
      {
        ret = real_stream_index;
        break;
      }
    }
  }
  else if (wanted_stream_id < nb_streams)
  {
    ret = wanted_stream_id;
  }

  return ret;
}

InputStream &InputFormat::addStream(const int wanted_stream_id,
                                    IAVFrameSinkBuffer &buf,
                                    const int related_stream_id)
{
  return add_stream(getStreamId(wanted_stream_id, related_stream_id), buf);
}

int InputFormat::getStreamId(const AVMediaType type,
                             const int related_stream_id) const
{
  return fmt_ctx ? av_find_best_stream(fmt_ctx, type, -1, related_stream_id,
                                       NULL, 0)
                 : AVERROR_STREAM_NOT_FOUND;
}

InputStream &InputFormat::addStream(const AVMediaType type,
                                    IAVFrameSinkBuffer &buf,
                                    const int related_stream_id)
{
  return add_stream(getStreamId(type, related_stream_id), buf);
}

int InputFormat::getStreamId(const std::string &spec,
                             const int related_stream_id) const
{
  int ret = AVERROR_STREAM_NOT_FOUND;
  if (!fmt_ctx) return ret; // if file not open, no stream avail

  const char *spec_str = spec.c_str();
  int nb_streams = fmt_ctx->nb_streams;
  if (related_stream_id >= 0)
  {
    unsigned *program = NULL;
    AVProgram *p =
        av_find_program_from_stream(fmt_ctx, NULL, related_stream_id);
    if (p)
    {
      program = p->stream_index;
      nb_streams = p->nb_stream_indexes;
    }
    for (int i = 0; ret < 0 && i < nb_streams; ++i)
    {
      int real_stream_index = program[i];
      AVStream *st = fmt_ctx->streams[real_stream_index];
      ret = avformat_match_stream_specifier(fmt_ctx, st, spec_str);
    }
  }
  else
  {
    for (int i = 0; ret < 0 && i < fmt_ctx->nb_streams; ++i)
      ret = avformat_match_stream_specifier(fmt_ctx, fmt_ctx->streams[i],
                                            spec_str);
  }

  return ret;
}

InputStream &InputFormat::addStream(const std::string &spec,
                                    IAVFrameSinkBuffer &buf,
                                    const int related_stream_id)
{
  int ret = getStreamId(spec, related_stream_id);
  return add_stream(ret, buf);
}
InputStream &InputFormat::add_stream(const int id, IAVFrameSinkBuffer &buf)
{
  if (id < 0) throw ffmpegException("Invalid stream ID specified.");

  if (streams.find(id) == streams.end())
  {
    AVStream *st = fmt_ctx->streams[id];
    AVCodecParameters *par = st->codecpar;
    switch (par->codec_type)
    {
    case AVMEDIA_TYPE_VIDEO:
      return *(streams.try_emplace(id, new InputVideoStream(*this, id, buf))
                   .first->second);
      break;
    case AVMEDIA_TYPE_AUDIO:
    case AVMEDIA_TYPE_DATA:
    case AVMEDIA_TYPE_SUBTITLE:
    case AVMEDIA_TYPE_ATTACHMENT:
    default: throw ffmpegException("Unsupported stream selected."); ;
    }
  }
  else
  {
    throw ffmpegException("Specified stream has already been activated.");
  }
}

void InputFormat::clearStreams()
{
  std::for_each(streams.begin(), streams.end(),
                [&](auto is) { delete is.second; });
  streams.clear();
}

InputStream &InputFormat::getStream(const int stream_id,
                                    const int related_stream_id)
{
  try
  {
    return *streams.at(getStreamId(stream_id, related_stream_id));
  }
  catch (const std::out_of_range &)
  {
    throw ffmpegException("Invalid/inactive stream ID");
  }
}

InputStream &InputFormat::getStream(const AVMediaType type,
                                    const int related_stream_id)
{
  try
  {
    return *streams.at(getStreamId(type, related_stream_id));
  }
  catch (const std::out_of_range &)
  {
    throw ffmpegException("Could not find matching active stream");
  }
}

InputStream &InputFormat::getStream(const std::string &spec,
                                    const int related_stream_id)
{
  try
  {
    return *streams.at(getStreamId(spec, related_stream_id));
  }
  catch (const std::out_of_range &)
  {
    throw ffmpegException(
        "Could not find matching active stream by the given specifier.");
  }
}

const InputStream &InputFormat::getStream(int stream_id,
                                          int related_stream_id) const
{
  auto it = streams.find(getStreamId(stream_id, related_stream_id));
  if (it == streams.end()) throw ffmpegException("Invalid/inactive stream ID");
  return *(it->second);
}

const InputStream &InputFormat::getStream(AVMediaType type,
                                          int related_stream_id) const
{
  try
  {
    return *streams.at(getStreamId(type, related_stream_id));
  }
  catch (const std::out_of_range &)
  {
    throw ffmpegException("Could not find matching active stream");
  }
}

const InputStream &InputFormat::getStream(const std::string &spec,
                                          int related_stream_id) const
{
  try
  {
    return *streams.at(getStreamId(spec, related_stream_id));
  }
  catch (const std::out_of_range &)
  {
    throw ffmpegException("Could not find matching active stream");
  }
}

int64_t InputFormat::getDurationTB() const
{
  // defined in us in the format context
  if (fmt_ctx)
    return fmt_ctx->duration +
           (fmt_ctx->duration <= INT64_MAX - 5000 ? 5000 : 0);
  else
    return AV_NOPTS_VALUE;
}

std::string InputFormat::getFilePath() const
{
  return fmt_ctx ? fmt_ctx->url : "";
}

AVRational InputFormat::getTimeBase() const
{
  return AVRational({AV_TIME_BASE, 1});
}

int64_t InputFormat::getCurrentTimeStampTB() const
{
  return (fmt_ctx) ? pts : AV_NOPTS_VALUE;
}

void InputFormat::setCurrentTimeStampTB(const int64_t seek_timestamp,
                                        const bool exact_search)
{
  if (!isFileOpen()) throw ffmpegException("No file open.");

  // if (val<0.0 || val>getDuration())
  //   throw ffmpegException("Out-of-range timestamp.");

  // AV_TIME_BASE

  // set new time
  // if filter graph changes frame rate -> convert it to the stream time
  int ret;
  if (ret = avformat_seek_file(fmt_ctx, -1, INT64_MIN, seek_timestamp,
                               seek_timestamp, 0) < 0)
    throw ffmpegException("Could not seek to position " +
                          std::to_string(seek_timestamp));

  // av_log(NULL,AV_LOG_INFO,"ffmpeg::InputFormat::setCurrentTimeStamp::seeking
  // %d\n",seek_timestamp);
  // av_log(NULL,AV_LOG_INFO,"ffmpeg::InputFormat::setCurrentTimeStamp::avformat_seek_file()
  // returned %d\n",ret);

  // avformat_seek_file() typically under-seeks, if exact_search requested, set
  // buf_start_ts to the requested timestamp (in output frame's timebase) to
  // make copy_frame_ts() to ignore all the frames prior to the requested
  if (exact_search)
  {
    std::for_each(streams.begin(), streams.end(), [&](auto pr) {
      pr.second->setStartTime(av_rescale_q(seek_timestamp, getTimeBase(),
                                           pr.second->getTimeBase()));
    });
  };
}

/////////////////////////////////////////////////////////////////////////////////////////////
// THREAD FUNCTIONS: Read file and send it to the FFmpeg decoder

InputStream *InputFormat::readNextPacket()
{
  int ret;

  if (!fmt_ctx) throw ffmpegException("No file open.");

  // unreference previously read packet
  av_packet_unref(&packet);

  // read the next frame packet
  ret = av_read_frame(fmt_ctx, &packet);
  if (ret == AVERROR_EOF) // must notify all streams
  {
    for (auto i = streams.begin(); i != streams.end(); ++i)
      i->second->processPacket(nullptr);
    return nullptr;
  }
  else if (ret > 0) // should not return EAGAIN
  {
    // work only on the registered streams
    InputStream *is = streams[packet.stream_index];
    ret = is->processPacket(&packet);
    if (ret < 0) throw ffmpegException(ret);

    // update pts
    // pts = av_rescale_q(is->getLastFrameTimeStamp(),
    // static_cast<ffmpeg::BaseStream*>(is)->getTimeBase(), AV_TIME_BASE_Q); //
    // a
    // * b / c
    pts = av_rescale_q(is->getLastFrameTimeStamp(), is->getTimeBase(),
                       AV_TIME_BASE_Q); // a * b / c

    return is;
  }
  else
    throw ffmpegException(ret);
}
