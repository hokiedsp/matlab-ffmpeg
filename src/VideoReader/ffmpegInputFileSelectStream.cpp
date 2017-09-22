#include "ffmpegInputFileSelectStream.h"

// #include <cstring>
#include <cmath>

extern "C" {
#include <libavutil/time.h>
#include <libavutil/pixdesc.h>
}

#include "../Common/ffmpegPtrs.h"
#include "../Common/ffmpegUtil.h"
#include "../Common/ffmpegAvRedefine.h"

#include <mex.h>

using namespace ffmpeg;

InputFileSelectStream::InputFileSelectStream()
    : fmt_ctx(NULL, delete_input_ctx), st(NULL), dec(NULL), dec_ctx(NULL, delete_codec_ctx),
      raw_packets(3), decoded_frames(3), filtered_frames(3), kill_threads(false), suspend_threads(false),
      read_state(-1), decode_state(-1), filter_state(-1),
      loop(0)
{
  // Creates an empty object

  // set predicates for the buffers to be able to kill their threads
  raw_packets.setPredicate(std::function<bool()>([&kill_threads = kill_threads]() { return kill_threads; }));
  decoded_frames.setPredicate(std::function<bool()>([&kill_threads = kill_threads]() { return kill_threads; }));
  filtered_frames.setPredicate(std::function<bool()>([&kill_threads = kill_threads]() { return kill_threads; }));
}

InputFileSelectStream::InputFileSelectStream(const std::string &filename, AVMediaType type, int st_index)
    : InputFileSelectStream()
{
  // create new file format context
  open_file(filename);

  // select a stream defined in the file as specified and create new codec context
  select_stream(type, st_index);

  // start decoding
  init_thread();
}

InputFileSelectStream::~InputFileSelectStream()
{
  free_thread();
}

bool InputFileSelectStream::eof() const { return false; }
double InputFileSelectStream::getDuration() const
{
  if (!fmt_ctx)
    return NAN;

  // defined in us in the format context
  double secs = NAN;
  if (fmt_ctx->duration != AV_NOPTS_VALUE)
  {
    int64_t duration = fmt_ctx->duration + (fmt_ctx->duration <= INT64_MAX - 5000 ? 5000 : 0);
    secs = double(duration / 100) / (AV_TIME_BASE / 100);
  }

  return secs;
}

std::string InputFileSelectStream::getFilePath() const { return ""; }
int InputFileSelectStream::getBitsPerPixel() const
{
  if (!fmt_ctx)
    return -1;

  if (dec_ctx->pix_fmt == AV_PIX_FMT_NONE)
    return -1;

  const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(dec_ctx->pix_fmt);
  if (pix_desc == NULL)
    return -1;
  return av_get_bits_per_pixel(pix_desc);
}

double InputFileSelectStream::getFrameRate() const
{
  if (fmt_ctx)
    return double(st->avg_frame_rate.num) / st->avg_frame_rate.den;
  else
    return NAN;
}

int InputFileSelectStream::getHeight() const
{
  if (fmt_ctx)
    return dec_ctx->height;
  else
    return -1;
}

int InputFileSelectStream::getWidth() const
{
  if (fmt_ctx)
    return dec_ctx->width;
  else
    return -1;
}

std::string InputFileSelectStream::getVideoPixelFormat() const
{
  if (fmt_ctx)
    return (dec_ctx->pix_fmt == AV_PIX_FMT_NONE) ? "none" : av_get_pix_fmt_name(dec_ctx->pix_fmt);
  else
    return "";
}

std::string InputFileSelectStream::getVideoCodecName() const
{
  if (fmt_ctx)
    return dec->name; // avcodec_get_name(dec_ctx->codec_id);
  else
    return "";
}

std::string InputFileSelectStream::getVideoCodecDesc() const
{
  if (fmt_ctx)
    return dec->long_name ? dec->long_name : "";
  else
    return "";
}

double InputFileSelectStream::getPTS() const
{
  if (!fmt_ctx)
    return NAN;

  // * @param s          media file handle
  // * @param stream     stream in the media file
  // * @param[out] dts   DTS of the last packet output for the stream, in stream
  // *                   time_base units
  // * @param[out] wall  absolute time when that packet whas output,
  // *                   in microsecond
  // int64_t dts, wall;
  // if (av_get_output_timestamp(fmt_ctx.get(), stream_index, &dts, &wall) < 0)
  //   throw ffmpegException("Failed to obtain the current timestamp.");
  // return double(dts / 100) / (AV_TIME_BASE / 100);
  return double(pts / 100) / (AV_TIME_BASE / 100);
}

uint64_t InputFileSelectStream::getNumberOfFrames() const { return 0; }

void InputFileSelectStream::setPTS(const double val)
{
  if (!fmt_ctx)
    throw ffmpegException("No file open.");

  int64_t seek_timestamp = int64_t(val * AV_TIME_BASE);

  if (!(fmt_ctx->iformat->flags & AVFMT_SEEK_TO_PTS) && st->codecpar->video_delay)
    seek_timestamp -= 3 * AV_TIME_BASE / 23;

  if (avformat_seek_file(fmt_ctx.get(), stream_index, INT64_MIN, seek_timestamp, seek_timestamp, 0) < 0)
    throw ffmpegException("Could not seek to position " + std::to_string(val) + " s");
}

///////////////////////////////

void InputFileSelectStream::open_file(const std::string &filename)
{
  if (filename.empty())
    throw ffmpegException("filename must be non-empty.");

  /* get default parameters from command line */
  fmt_ctx.reset(avformat_alloc_context());
  if (!fmt_ctx.get())
    throw ffmpegException(filename, ENOMEM);

  fmt_ctx->flags |= AVFMT_FLAG_NONBLOCK;
  fmt_ctx->interrupt_callback = {NULL, NULL}; // from ffmpegBase

  ////////////////////

  AVDictionary *d = NULL;
  av_dict_set(&d, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);

  /* open the input file with generic avformat function */
  AVFormatContext *ic = fmt_ctx.release();
  int err;
  if ((err = avformat_open_input(&ic, filename.c_str(), NULL, &d)) < 0)
    throw ffmpegException(filename, err);
  fmt_ctx.reset(ic);

  if (d)
    av_dict_free(&d);

  /* If not enough info to get the stream parameters, we decode the
       first frames to get it. (used in mpeg case for example) */
  if (avformat_find_stream_info(ic, NULL) < 0)
    throw ffmpegException("Could not find codec parameters");

  // initialize frame counter
  pts = 0;
  frames_decoded = 0;
  samples_decoded = 0;

}

/* Add all the streams from the given input file to the global
 * list of input streams. */
void InputFileSelectStream::select_stream(AVMediaType type, int index)
{
  if (!fmt_ctx)
    throw ffmpegException("Cannot select a stream: No file open.");

  // // potentially use this function instead
  // int av_find_best_stream(AVFormatContext *ic, enum AVMediaType type,
  //   int wanted_stream_nb, int related_stream, AVCodec **decoder_ret, int flags);

  AVFormatContext *ic = fmt_ctx.get();

  int count = 0;
  for (int i = 0; i < (int)ic->nb_streams; i++) // for each stream
  {
    // look for a stream which matches the
    if (ic->streams[i]->codecpar->codec_type == type && count++ == index)
    {
      stream_index = i;
      st = ic->streams[i];
      switch (type)
      {
      case AVMEDIA_TYPE_VIDEO:
      case AVMEDIA_TYPE_AUDIO:
        break;
      case AVMEDIA_TYPE_SUBTITLE:
      case AVMEDIA_TYPE_DATA:
      case AVMEDIA_TYPE_ATTACHMENT:
      case AVMEDIA_TYPE_UNKNOWN:
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
  if (!count)
    ffmpegException("Media file does not include the requested media type.");

  // set codec
  dec = avcodec_find_decoder(st->codecpar->codec_id);

  // create decoder context
  dec_ctx.reset(avcodec_alloc_context3(dec));
  if (!dec_ctx)
    throw ffmpegException("Error allocating the decoder context.");

  // set the stream parameters to the decoder context
  if (avcodec_parameters_to_context(dec_ctx.get(), st->codecpar) < 0)
    throw ffmpegException("Error initializing the decoder context.");

  // do additional codec context configuration if needed
  switch (type)
  {
  case AVMEDIA_TYPE_VIDEO:
    dec_ctx->framerate = st->avg_frame_rate;
    break;
  case AVMEDIA_TYPE_AUDIO:
  case AVMEDIA_TYPE_SUBTITLE:
  case AVMEDIA_TYPE_DATA:
  case AVMEDIA_TYPE_ATTACHMENT:
  case AVMEDIA_TYPE_UNKNOWN:
  default:
    break;
  }
}

/////////////////////////////////////////////////////////

void InputFileSelectStream::init_thread()
{
  mexPrintf("Starting packet_reader thread.\n");
  try
  {
    read_thread = std::thread(&InputFileSelectStream::read_thread_fcn, this);
    decode_thread = std::thread(&InputFileSelectStream::decode_thread_fcn, this);
  }
  catch (std::exception &e)
  {
    mexPrintf("Failed to start the thread: %s", e.what());
  }
  // decode_thread = std::thread(&InputFileSelectStream::decode_thread_fcn, this);
}

void InputFileSelectStream::free_thread(void)
{
  mexPrintf("Terminating packet_reader thread.\n");

  kill_threads = true;
  raw_packets.releaseAll();
  decoded_frames.releaseAll();
  filtered_frames.releaseAll();

  suspend_threads = false;
  suspend_cv.notify_all();

  // stop the threads
  read_thread.join();
  decode_thread.join();
  // filter_thread.join();

  // clear buffers
  // filtered_frames.reset();
  decoded_frames.reset();
  raw_packets.reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////

void InputFileSelectStream::read_thread_fcn()
{
  read_state = 0;
  AVFormatContext *is = fmt_ctx.get();

  int ctr = 0;
  try
  {
    // kill_threads    - immediate termination
    // suspend_threads - pause the thread loops until released

    while (!kill_threads)
    {
      // if video reading operation is suspended, pause the thread until notified
      if (suspend_threads || eof_reached)
      {
        std::unique_lock<std::mutex> lk(suspend_mutex);
        suspend_cv.wait(lk, [this] { return !(eof_reached && suspend_threads) || kill_threads; });
        if (kill_threads)
          break;
      }

      mexPrintf("Reader [%d] Reserving an element in the packet queue.\n", ctr);
      AVPacket &pkt = raw_packets.enqueue_new();
      if (kill_threads)
      {
        raw_packets.enqueue_complete();
        break;
      }

      // read next frame
      mexPrintf("Reader [%d] Reading the next frame from file.\n", ctr);
      read_state = av_read_frame(is, &pkt);

      // if any type of read error occurred, release the reserved queue element
      if (read_state < 0 || pkt.buf == NULL)
        raw_packets.enqueue_cancel();

      if (read_state == AVERROR(EAGAIN)) // frame not ready, wait 10 ms
      {
        mexPrintf("Reader [%d] Frame not ready; wait a little while and try again...\n", ctr);
        av_usleep(10000);
        continue;
      }

      // reached eof-of-file:
      if (read_state == AVERROR_EOF || pkt.buf == NULL)
      {
        if (loop < 0 || (loop--) > 0) // 0:no loop, >0: finite loop, <0: infinite loop
        {
          mexPrintf("Reader [%d] EOF; Rewinding to loop...\n", ctr);
          // rewind
          read_state = av_seek_frame(is, -1, is->start_time, 0);
          mexPrintf("Reader [%d] EOF; Releasing the reserved queue element...\n", ctr);

          // reset the counter
          frames_decoded = 0;
          samples_decoded = 0;
          pts = 0;
        }
        else // stop
        {
          mexPrintf("Reader [%d] EOF; Stopping the thread\n", ctr);
          eof_reached = true;
          read_state = 0;
        }
        continue;
      }

      // unexcepted error occurred, terminate the thread
      if (read_state < 0)
      {
        mexPrintf("Reader [%d] Unexpected error has occurred\n", ctr);
        break;
      }

      // if received packet is not of the stream, read next packet
      if (pkt.stream_index != stream_index)
      {
        mexPrintf("Reader [%d] Frame is not for the selected stream\n", ctr);
        av_packet_unref(&pkt);
        raw_packets.enqueue_cancel();
        continue;
      }

      // place the new packet on the buffer
      mexPrintf("Reader [%d] Finalize queing of the frame \n", ctr);
      raw_packets.enqueue_complete();

      mexPrintf("Reader [%d] Frame is successfully placed on the queue\n", ctr++);

      if (ctr > 3)
        return; // debug
    }
  }
  catch (std::exception &e)
  {
    mexPrintf("Exception occurred in packet_reader thread: %s\n", e.what());
  }
}

void InputFileSelectStream::decode_thread_fcn()
{
  int ret;
  AVCodecContext *avctx = dec_ctx.get();
  decode_state = 0;
  bool got_output = false;
  int ctr = 0;
  try
  {
    while (!kill_threads)
    {
      // if video reading operation is suspended, pause the thread until notified
      if (suspend_threads)
      {
        std::unique_lock<std::mutex> lk(suspend_mutex);
        suspend_cv.wait(lk, [this] { return !suspend_threads || kill_threads; });
        if (kill_threads)
          break;
      }

      // read next packet (blocks until next packet available)
      mexPrintf("Decoder [%d] Getting the next packet.\n", ctr);
      AVPacket *pkt = &raw_packets.peek_next();
      if (suspend_threads || kill_threads) continue;

      // send packet to the decoder
      mexPrintf("Decoder [%d] Sending the packet to the FFmpeg decoder.\n", ctr);
      ret = avcodec_send_packet(avctx, pkt);
      if (ret < 0)
        throw ffmpegException(av_err2str(ret));
      if (suspend_threads || kill_threads) continue;
        
      // receive frames until there are no more
      int fctr = 0;
      while (ret != AVERROR(EAGAIN))
      {
        // create a new frame queue entry to accept the first frame of the packet
        mexPrintf("Decoder [%d:%d] Reserving the buffer space for the next frame.\n", ctr,fctr);
        AVFrame *frame = decoded_frames.enqueue_new();
        if (suspend_threads || kill_threads) continue;
        
        mexPrintf("Decoder [%d:%d] Receiving the decoded frame from FFmpeg decoder.\n", ctr,fctr);
        ret = avcodec_receive_frame(avctx, frame);

        if (ret < 0) // if no frame decoded, cancel enqueue
        {
          decoded_frames.enqueue_cancel();
          if (suspend_threads || kill_threads) continue;
          if (ret == AVERROR(EAGAIN))
          {
            mexPrintf("Decoder [%d:%d] All the frames decoded for the current packet.\n", ctr,fctr);
            continue;
          }
          else
          {
            mexPrintf("Decoder [%d:%d] Something went wrong.\n", ctr, fctr);
            throw ffmpegException(av_err2str(ret));
          }
        }

        // update stats
        mexPrintf("Decoder [%d:%d] Update the frame count.\n", ctr,fctr);
        frames_decoded++;
        samples_decoded += frame->nb_samples;

        // new frame data are now populated
        mexPrintf("Decoder [%d:%d] Releasing the buffer element.\n", ctr,fctr);
        decoded_frames.enqueue_complete();
        if (suspend_threads || kill_threads) continue;
      }
      if (suspend_threads || kill_threads) continue;
      
      // release the packet (may not need to wait until the end of decoding...)
      mexPrintf("Decoder [%d] Releasing the packet element.\n", ctr);
      raw_packets.dequeue(true); // was_peeking=true 
    }
  }
  catch (std::exception &e)
  {
    mexPrintf("Exception occurred in decode_thread thread: %s\n", e.what());
  }
}

// void InputFileSelectStream::filter_audio()
//    // if decoded audio format is different from expected, configure a filter graph to resample
//    bool resample_changed = resample_sample_fmt != decoded_frame->format || resample_channels != avctx->channels ||
//                       resample_channel_layout != decoded_frame->channel_layout || resample_sample_rate != decoded_frame->sample_rate;
//    if (resample_changed)
//    {
//       if (!guess_input_channel_layout(ist))
//          ffmpegException("Unable to find default channel layout\n");

//       decoded_frame->channel_layout = avctx->channel_layout;

//       resample_sample_fmt = decoded_frame->format;
//       resample_sample_rate = decoded_frame->sample_rate;
//       resample_channel_layout = decoded_frame->channel_layout;
//       resample_channels = avctx->channels;

//       for (i = 0; i < nb_filtergraphs; i++)
//          if (ist_in_filtergraph(filtergraphs[i], ist))
//          {
//             FilterGraph *fg = filtergraphs[i];
//             if (configure_filtergraph(fg) < 0)
//                ffmpegException("Error reinitializing filters!\n");
//          }
//    }

//    AVFrame *filter_frame = av_frame_alloc();

//    nb_samples = decoded_frame->nb_samples;
//    for (i = 0; i < ist->nb_filters; i++) // for each filter graph
//    {
//       if (i < ist->nb_filters - 1)
//       {
//          f = ist->filter_frame;
//          err = av_frame_ref(f, decoded_frame);
//          if (err < 0)
//             break;
//       }
//       else
//          f = decoded_frame;
//       err = av_buffersrc_add_frame_flags(ist->filters[i]->filter, f,
//                                          AV_BUFFERSRC_FLAG_PUSH);
//       if (err == AVERROR_EOF)
//          err = 0; /* ignore */
//       if (err < 0)
//          break;
//    }
//    decoded_frame->pts = AV_NOPTS_VALUE;

//    av_frame_unref(filter_frame);
//    av_frame_unref(decoded_frame);
//    return err < 0 ? err : ret;
// }


// // void InputFileSelectStream::filter_video()
// // {
// //    if (st->sample_aspect_ratio.num)
// //       decoded_frame->sample_aspect_ratio = st->sample_aspect_ratio;

// //       if (!ist->filter_frame && !(ist->filter_frame = av_frame_alloc()))
// //       return AVERROR(ENOMEM);
// //    resample_changed = ist->resample_width != decoded_frame->width ||
// //                       ist->resample_height != decoded_frame->height ||
// //                       ist->resample_pix_fmt != decoded_frame->format;
// //    if (resample_changed)
// //    {
// //       av_log(NULL, AV_LOG_INFO,
// //              "Input stream #%d:%d frame changed from size:%dx%d fmt:%s to size:%dx%d fmt:%s\n",
// //              ist->file_index, st->index,
// //              ist->resample_width, ist->resample_height, av_get_pix_fmt_name(ist->resample_pix_fmt),
// //              decoded_frame->width, decoded_frame->height, av_get_pix_fmt_name(decoded_frame->format));

// //       ist->resample_width = decoded_frame->width;
// //       ist->resample_height = decoded_frame->height;
// //       ist->resample_pix_fmt = decoded_frame->format;

// //       for (i = 0; i < nb_filtergraphs; i++)
// //       {
// //          if (ist_in_filtergraph(filtergraphs[i], ist) && ist->reinit_filters &&
// //              configure_filtergraph(filtergraphs[i]) < 0)
// //          {
// //             ffmpegException("Error reinitializing filters!");
// //          }
// //       }
// //    }

// //    frame_sample_aspect = av_opt_ptr(avcodec_get_frame_class(), decoded_frame, "sample_aspect_ratio");
// //    for (i = 0; i < ist->nb_filters; i++)
// //    {
// //       if (!frame_sample_aspect->num)
// //          *frame_sample_aspect = st->sample_aspect_ratio;

// //       if (i < ist->nb_filters - 1)
// //       {
// //          f = ist->filter_frame;
// //          err = av_frame_ref(f, decoded_frame);
// //          if (err < 0)
// //             break;
// //       }
// //       else
// //          f = decoded_frame;
// //       err = av_buffersrc_add_frame_flags(ist->filters[i]->filter, f, AV_BUFFERSRC_FLAG_PUSH);
// //       if (err == AVERROR_EOF)
// //       {
// //          err = 0; /* ignore */
// //       }
// //       else if (err < 0)
// //       {
// //          ffmpegException("Failed to inject frame into filter network: %s", av_err2str(err));
// //       }
// //    }

// // fail:
// //    av_frame_unref(ist->filter_frame);
// //    av_frame_unref(decoded_frame);
// //    return err < 0 ? err : ret;
// // }

// bool InputFileSelectStream::check_decode_result(AVFrame *decoded_frame, bool got_output, int ret)
// {

//    if (got_output || ret < 0) decode_error_stat[ret < 0]++;

//    bool rval = got_output && (ret != AVERROR_EOF) && (av_frame_get_decode_error_flags(decoded_frame) || (decoded_frame->flags & AV_FRAME_FLAG_CORRUPT));
// }

// bool InputFileSelectStream::guess_input_channel_layout()
// {
//    // if channel layout is not given, make a guess
//    if (!dec_ctx->channel_layout)
//    {
//       if (dec_ctx->channels > guess_layout_max)
//          return false;
//       dec_ctx->channel_layout = av_get_default_channel_layout(dec_ctx->channels);
//       if (!dec_ctx->channel_layout)
//          return false;
//    }
//    return true;
// }

// ////////////////////////////

// void InputFileSelectStream::seek(const int64_t timestamp)
// {
//    // stop the threads

//    int64_t seek_timestamp = timestamp;
//    if (!(fmt_ctx->iformat->flags & AVFMT_SEEK_TO_PTS)) // if seeking is not based on presentation timestamp (PTS)
//    {
//       bool dts_heuristic = false;
//       for (int i = 0; i < (int)fmt_ctx->nb_streams; i++)
//       {
//          if (fmt_ctx->streams[i]->codecpar->video_delay)
//             dts_heuristic = true;
//       }
//       if (dts_heuristic)
//          seek_timestamp -= 3 * AV_TIME_BASE / 23;
//    }

//    if (avformat_seek_file(fmt_ctx.get(), stream.index, INT64_MIN, seek_timestamp, seek_timestamp, 0) < 0)
//    {
//       std::ostringstream msg;
//       msg << "Could not seek to position " << (double)timestamp / AV_TIME_BASE;
//       throw ffmpegException(msg.str());
//    }

//    // restart the threads

// }

// /*
//  * Return
//  * - 0 -- one packet was read and processed
//  * - AVERROR(EAGAIN) -- no packets were available for selected file,
//  *   this function should be called again
//  * - AVERROR_EOF -- this function should not be called again
//  */
// void InputFileSelectStream::prepare_packet(AVPacket &pkt)
// {
//    if (pkt.stream_index == stream_index)
//       stream.prepare_packet(&pkt, false); // decode if requested
// }

// int InputFileSelectStream::get_packet(AVPacket &pkt)
// {
// #define RECVPKT(p) av_thread_message_queue_recv(in_thread_queue, &p, non_blocking ? AV_THREAD_MESSAGE_NONBLOCK : 0)

//    // receive the frame/packet from one of input threads
//    int ret = RECVPKT(pkt);
//    if (ret == AVERROR(EAGAIN)) // frame not available now
//    {
//       eagain = true;
//       return ret;
//    }
//    if (ret == AVERROR_EOF && loop) // if reached EOF and loop playback mode
//    {
//       seek(0);            // rewind the stream
//       ret = RECVPKT(pkt); // see if the first frame is already available
//       if (ret == AVERROR(EAGAIN))
//       {
//          eagain = true;
//          return ret;
//       }
//    }
//    if (ret == AVERROR_EOF) // if end-of-file
//    {
//       // if end-of-file reached, flush all input & output streams
//       stream.flush(false);

//       eof_reached = true;
//       return AVERROR(EAGAIN);
//    }
//    else if (ret < 0) // if not end-of-file, a fatal error has occurred
//    {
//       throw ffmpegException(fmt_ctx->filename, ret);
//    }
// }
