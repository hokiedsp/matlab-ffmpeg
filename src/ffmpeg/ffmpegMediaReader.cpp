#include "ffmpegMediaReader.h"

#include "ffmpegException.h"
#include "ffmpegPtrs.h"
#include "ffmpegAvRedefine.h"

extern "C"
{
#include <libavutil/mathematics.h>
}

#include <algorithm>

using namespace ffmpeg;

MediaReader::MediaReader(const std::string &filename, const AVMediaType type)
    : fmt_ctx(NULL), pts(AV_NOPTS_VALUE)
{
  // initialize to allow preemptive unreferencing
  if (!filename.empty())
    openFile(filename, type);

  // initialize packet struct
  av_init_packet(&packet);
}

MediaReader::~MediaReader()
{
  // release whatever currently is in the packet holder
  av_packet_unref(&packet);

  // close the file
  closeFile();
}

////////////////////////////////////////////////////////////////////////////////////////////////

bool MediaReader::isFileOpen() { return fmt_ctx; }
bool MediaReader::EndOfFile() { return fmt_ctx && pts >= getDuration(); }

void MediaReader::openFile(const std::string &filename, const AVMediaType type)
{
  int ret;

  if (fmt_ctx)
    throw ffmpegException("Another file already open. Close it first.");

  if ((ret = avformat_open_input(&fmt_ctx, filename.c_str(), NULL, NULL)) < 0)
    throw ffmpegException("Cannot open input file");

  if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
    throw ffmpegException("Cannot find stream information");

  // initially set to ignore all other streams (an InputStream object sets it to AVDISCARD_NONE when it opens a stream)
  for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i)
    fmt_ctx->streams[i]->discard = AVDISCARD_ALL;

  // if stream type is specified, pick the best stream
  if (type != AVMEDIA_TYPE_UNKNOWN)
  {
    int ret = addStream(type);
    if (ret < 0)
    {
      std::string msg = "The input file does not contain any ";
      msg += av_get_media_type_string(type);
      msg += " stream in the input file";
      throw ffmpegException(msg);
    }
  }
}

void MediaReader::closeFile()
{
  // nothing to do if file is not open
  if (!isFileOpen())
    return;

  // close all the streams
  clearStreams();

  // close the file
  if (fmt_ctx)
    avformat_close_input(&fmt_ctx);
}

////////////////////////////////////////////////////////////////////////////////////////////////

int MediaReader::addStream(int wanted_stream_id, int related_stream_id)
{
  int ret = AVERROR_STREAM_NOT_FOUND;
  int nb_streams = fmt_ctx->nb_streams;
  if (related_stream_id >= 0)
  {
    unsigned *program = NULL;
    AVProgram *p = av_find_program_from_stream(fmt_ctx, NULL, related_stream_id);
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

  if (ret >= 0)
    add_stream(ret);

  return ret;
}
int MediaReader::addStream(AVMediaType type, int related_stream_id)
{
  int ret = av_find_best_stream(fmt_ctx, type, -1, related_stream_id, NULL, 0);
  if (ret >= 0)
    add_stream(ret);

  return ret;
}
void MediaReader::addAllStreams()
{
  for (int i = 0; i < (int)fmt_ctx->nb_streams; ++i)
    add_stream(i);
}

void MediaReader::add_stream(const int id)
{
  AVStream *st = fmt_ctx->streams[id];
  AVCodecParameters *par = st->codecpar;
  switch (par->codec_type)
  {
  case AVMEDIA_TYPE_VIDEO:
    streams.insert(std::make_pair(id, new InputVideoStream(st)));
    break;
  case AVMEDIA_TYPE_AUDIO:
  case AVMEDIA_TYPE_DATA:
  case AVMEDIA_TYPE_SUBTITLE:
  case AVMEDIA_TYPE_ATTACHMENT:
  default:
    streams.insert(std::make_pair(id, new InputStream(st)));
  }
}

void MediaReader::clearStreams()
{
  std::for_each(streams.begin(), streams.end(), [&](auto is) { delete is.second; });
  streams.clear();
}

InputStream &MediaReader::getStream(int stream_id)
{
  try
  {
    return *streams.at(stream_id);
  }
  catch(const std::out_of_range& e)
  {
    throw ffmpegException("Invalid/inactive stream ID");
  }
}

InputStream &MediaReader::getStream(AVMediaType type, int related_stream_id)
{
  int ret = av_find_best_stream(fmt_ctx, type, -1, related_stream_id, NULL, 0);
  if (ret < 0)
    return getStream(ret);
  throw ffmpegException("Could not find matching stream in the media file");
}

const InputStream &MediaReader::getStream(int stream_id) const
{
  auto it = streams.find(stream_id);
  if (it == streams.end())
    throw ffmpegException("Invalid/inactive stream ID");
  return *(it->second);
}

const InputStream &MediaReader::getStream(AVMediaType type, int related_stream_id) const
{
  int ret = av_find_best_stream(fmt_ctx, type, -1, related_stream_id, NULL, 0);
  if (ret < 0)
    return getStream(ret);
  throw ffmpegException("Could not find matching stream in the media file");
}

int64_t MediaReader::getDuration() const
{
  // defined in us in the format context
  if (fmt_ctx)
    return fmt_ctx->duration;
  else
    return AV_NOPTS_VALUE;
}

std::string MediaReader::getFilePath() const
{
  return fmt_ctx ? fmt_ctx->url : "";
}

AVRational MediaReader::getTimeBase() const
{
  return AVRational({AV_TIME_BASE, 1});
}

int64_t MediaReader::getCurrentTimeStamp() const
{
  return (fmt_ctx) ? pts : AV_NOPTS_VALUE;
}

void MediaReader::setCurrentTimeStamp(const int64_t seek_timestamp, const bool exact_search)
{
  if (!isFileOpen())
    throw ffmpegException("No file open.");

  // if (val<0.0 || val>getDuration())
  //   throw ffmpegException("Out-of-range timestamp.");

  // AV_TIME_BASE

  // set new time
  // if filter graph changes frame rate -> convert it to the stream time
  int ret;
  if (ret = avformat_seek_file(fmt_ctx, -1, INT64_MIN, seek_timestamp, seek_timestamp, 0) < 0)
    throw ffmpegException("Could not seek to position " + std::to_string(seek_timestamp));

  // av_log(NULL,AV_LOG_INFO,"ffmpeg::MediaReader::setCurrentTimeStamp::seeking %d\n",seek_timestamp);
  // av_log(NULL,AV_LOG_INFO,"ffmpeg::MediaReader::setCurrentTimeStamp::avformat_seek_file() returned %d\n",ret);

  // avformat_seek_file() typically under-seeks, if exact_search requested, set buf_start_ts to the
  // requested timestamp (in output frame's timebase) to make copy_frame_ts() to ignore all the frames prior to the requested
  if (exact_search)
  {
    std::for_each(streams.begin(), streams.end(),
                  [&](auto pr) { pr.second->setStartTime(av_rescale_q(seek_timestamp, getTimeBase(), pr.second->getTimeBase())); });
  };
}

/////////////////////////////////////////////////////////////////////////////////////////////
// THREAD FUNCTIONS: Read file and send it to the FFmpeg decoder

void MediaReader::readNextPacket()
{
  int ret;

  // unreference previously read packet
  av_packet_unref(&packet);

  // read the next frame packet
  ret = av_read_frame(fmt_ctx, &packet);
  bool eof = ret == AVERROR_EOF;
  if (ret < 0 && !eof) // should not return EAGAIN
    throw ffmpegException(ret);

  // work only on the registered streams
  InputStream &is = getStream(packet.stream_index);
  ret = is.processPacket(eof ? NULL : &packet);
  if (ret < 0)
    throw ffmpegException(ret);

  // update pts
  pts = av_rescale_q(is.getLastFrameTimeStamp(), is.getTimeBase(), getTimeBase());
}
