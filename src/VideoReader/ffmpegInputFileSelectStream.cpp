#include "ffmpegInputFile.h"

#include <cstring>

extern "C" {
#include <libavutil/time.h>
}

#include "ffmpegPtrs.h"
#include "ffmpegUtil.h"
#include "ffmpegAvRedefine.h"

using namespace ffmpeg;

InputFileSelectStream::InputFileSelectStream(const std::string &filename, AVMediaType type, int st_index)
    : ctx(NULL, delete_input_ctx), accurate_seek(0), loop(0),
      thread_queue_size(8), non_blocking(false)
{
  /* get default parameters from command line */
  ctx.reset(avformat_alloc_context());
  if (!ctx.get())
    throw ffmpegException(filename, AVERROR(ENOMEM));

  // set format's codecs to the last forced
  //  opt_str = o.gettype<SpecifierOptsString, std::string>("codec", "v");
  //  if (opt_str->size())
  //  {
  //     AVCodec *c = find_decoder(*opt_str, AVMEDIA_TYPE_VIDEO); // always return non-NULL
  //     ctx->video_codec_id = c->id;
  //     av_format_set_video_codec(ctx.get(), c);
  //  }
  ctx->video_codec_id = AV_CODEC_ID_NONE;

  //  opt_str = o.gettype<SpecifierOptsString, std::string>("codec", "a");
  //  if (opt_str)
  //  {
  //     AVCodec *c = find_decoder(*opt_str, AVMEDIA_TYPE_AUDIO); // always return non-NULL
  //     ctx->audio_codec_id = c->id;
  //     av_format_set_audio_codec(ctx.get(), c);
  //  }
  ctx->audio_codec_id = AV_CODEC_ID_NONE;

  //  opt_str = o.gettype<SpecifierOptsString, std::string>("codec", "s");
  //  if (opt_str)
  //  {
  //     AVCodec *c = find_decoder(*opt_str, AVMEDIA_TYPE_SUBTITLE); // always return non-NULL
  //     ctx->subtitle_codec_id = c->id;
  //     av_format_set_subtitle_codec(ctx.get(), c);
  //  }
  ctx->subtitle_codec_id = AV_CODEC_ID_NONE;

  //  opt_str = o.gettype<SpecifierOptsString, std::string>("codec", "d");
  //  if (opt_str)
  //  {
  //     AVCodec *c = find_decoder(*opt_str, AVMEDIA_TYPE_DATA); // always return non-NULL
  //     ctx->data_codec_id = c->id;
  //     av_format_set_data_codec(ctx.get(), c);
  //  }
  ctx->data_codec_id = AV_CODEC_ID_NONE;

  ctx->flags |= AVFMT_FLAG_NONBLOCK;
  ctx->interrupt_callback = int_cb; // from ffmpegBase

  ////////////////////

  DictPtr format_opts = std::make_unique<AVDictionary, delete_dict>();
  av_dict_set(*format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);

  /* open the input file with generic avformat function */
  AVFormatContext *ic = ctx.release();
  int err;
  if ((err = avformat_open_input(&ic, filename.c_str(), NULL, &format_opts)) < 0)
    throw ffmpegException(filename, err);
  ctx.reset(ic);

  ////////////////////

  /* update the current parameters so that they match the one of the input stream */
  select_stream(type, st_index);
}

/* Add all the streams from the given input file to the global
 * list of input streams. */
void InputFileSelectStream::select_stream(AVMediaType type, int index)
{
  AVFormatContext *ic = ctx.get();

  int i = 0;
  int count = 0;
  for (; i < (int)ic->nb_streams; i++) // for each stream
  {
    // look for a stream which matches the
    if (ic->streams[i]->codecpar->codec_type == type && count++ == index)
    {
      stream_index = i;
      switch (type)
      {
      case AVMEDIA_TYPE_VIDEO:
        stream = new VideoInputStream(*this, i, NULL);
        break;
      case AVMEDIA_TYPE_AUDIO:
        stream = new AudioInputStream(*this, i, NULL);
        break;
      case AVMEDIA_TYPE_SUBTITLE:
        stream = new SubtitleInputStream(*this, i, NULL);
        break;
      case AVMEDIA_TYPE_DATA:
        stream = new DataInputStream(*this, i, NULL);
        break;
      case AVMEDIA_TYPE_ATTACHMENT:
      case AVMEDIA_TYPE_UNKNOWN:
        stream = new InputStream(*this, i, NULL);
        break;
      default:
        throw ffmpegException("Unsupported decoder media type.");
      }
    }
    else
    {
      // all other streams are ignored
      ic->streams[i]->discard = AVDISCARD_ALL;
    }
  }
}
}

////////////////////////////

void InputFileSelectStream::seek(const int64_t timestamp)
{
  int64_t seek_timestamp = timestamp;
  if (!(ctx->iformat->flags & AVFMT_SEEK_TO_PTS)) // if seeking is not based on presentation timestamp (PTS)
  {
    bool dts_heuristic = false;
    for (int i = 0; i < (int)ctx->nb_streams; i++)
    {
      if (ctx->streams[i]->codecpar->video_delay)
        dts_heuristic = true;
    }
    if (dts_heuristic)
      seek_timestamp -= 3 * AV_TIME_BASE / 23;
  }

  if (avformat_seek_file(ctx.get(), stream.index, INT64_MIN, seek_timestamp, seek_timestamp, 0) < 0)
  {
    std::ostringstream msg;
    msg << "Could not seek to position " << (double)timestamp / AV_TIME_BASE;
    throw ffmpegException(msg.str());
  }
}

////////////////////////////

int InputFileSelectStream::get_packet(AVPacket &pkt)
{
#define RECVPKT(p) av_thread_message_queue_recv(in_thread_queue, &p, non_blocking ? AV_THREAD_MESSAGE_NONBLOCK : 0)

  // receive the frame/packet from one of input threads
  int ret = RECVPKT(pkt);
  if (ret == AVERROR(EAGAIN)) // frame not available now
  {
    eagain = true;
    return ret;
  }
  if (ret == AVERROR_EOF && loop) // if reached EOF and loop playback mode
  {
    seek(0); // rewind the stream
    ret = RECVPKT(pkt); // see if the first frame is already available
    if (ret == AVERROR(EAGAIN))
    {
      eagain = true;
      return ret;
    }
  }
  if (ret == AVERROR_EOF) // if end-of-file
  {
    // if end-of-file reached, flush all input & output streams
    stream.flush(false);

    eof_reached = true;
    return AVERROR(EAGAIN);
  }
  else if (ret < 0) // if not end-of-file, a fatal error has occurred
  {
    throw ffmpegException(ctx->filename, ret);
  }
}

/*
 * Return
 * - 0 -- one packet was read and processed
 * - AVERROR(EAGAIN) -- no packets were available for selected file,
 *   this function should be called again
 * - AVERROR_EOF -- this function should not be called again
 */
void InputFileSelectStream::prepare_packet(AVPacket &pkt)
{
  if (pkt.stream_index == stream_index)
      stream.prepare_packet(&pkt, false); // decode if requested
}

/////////////////////////////////////////////////////////

void InputFileSelectStream::input_thread()
{
  unsigned flags = non_blocking ? AV_THREAD_MESSAGE_NONBLOCK : 0;
  int ret = 0;

  while (1)
  {
    AVPacket pkt;

    // read next frame
    ret = av_read_frame(ctx.get(), &pkt);
    if (ret == AVERROR(EAGAIN)) // frame not ready, wait 10 ms
    {
      av_usleep(10000);
      continue;
    }
    if (ret < 0) // unexcepted error occurred, throw the error over the message queue and quit
    {
      av_thread_message_queue_set_err_recv(in_thread_queue, ret);
      break;
    }

    // if received packet is not of the stream, read next packet
    if (pkt.stream_index != stream_index)
      continue;    

    // send decoded frame to the message queue
    ret = av_thread_message_queue_send(in_thread_queue, &pkt, flags);
    if (flags && ret == AVERROR(EAGAIN)) // queue overflowed
    { // try it again BLOCKING
      flags = 0;
      ret = av_thread_message_queue_send(in_thread_queue, &pkt, flags);
      //av_log(ctx, AV_LOG_WARNING, "Thread message queue blocking; consider raising the thread_queue_size option (current value: %d)\n", thread_queue_size);
    }
    if (ret < 0)
    {
      // if (ret != AVERROR_EOF)
      //    av_log(ctx, AV_LOG_ERROR, "Unable to send packet to main thread: %s\n", av_err2str(ret));
      av_packet_unref(&pkt); // let go of the packet object
      av_thread_message_queue_set_err_recv(in_thread_queue, ret);
      break;
    }
  }
}

void InputFileSelectStream::init_thread()
{
  if (ctx->pb ? !ctx->pb->seekable : std::strcmp(ctx->iformat->name, "lavfi"))
    non_blocking = true;

  int ret;
  if (ret = av_thread_message_queue_alloc(&in_thread_queue, thread_queue_size, sizeof(AVPacket)) < 0)
    throw ffmpegException(ret);

  thread = std::thread(&InputFileSelectStream::input_thread, this);
}

void InputFileSelectStream::free_thread(void)
{
  AVPacket pkt;

  if (!in_thread_queue)
    return;

  av_thread_message_queue_set_err_send(in_thread_queue, AVERROR_EOF);
  while (av_thread_message_queue_recv(in_thread_queue, &pkt, 0) >= 0)
    av_packet_unref(&pkt);

  thread.join();
  joined = true;
  av_thread_message_queue_free(&in_thread_queue);
}
