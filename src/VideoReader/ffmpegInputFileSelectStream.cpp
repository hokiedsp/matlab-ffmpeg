#include "ffmpegInputFileSelectStream.h"

// #include <cstring>
#include <cmath>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavutil/pixdesc.h>
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
}

#include "../Common/ffmpegPtrs.h"
#include "../Common/ffmpegUtil.h"
#include "../Common/ffmpegAvRedefine.h"

#include <mex.h>

using namespace ffmpeg;

InputFileSelectStream::InputFileSelectStream()
    : fmt_ctx(NULL), st(NULL), dec(NULL), dec_ctx(NULL),
      raw_packets(3), decoded_frames(3), filtered_frames(3), kill_threads(false), suspend_threads(false),
      read_state(-1), decode_state(-1), filter_state(-1),
      loop(0), eof_reached(false)
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

  // initialize the decoder for the stream
  init_stream();

  // start decoding
  init_thread();
}

InputFileSelectStream::~InputFileSelectStream()
{
  mexPrintf("deconstructor::freeing threads\n");
  free_thread();
  mexPrintf("deconstructor::threads freed\n");

  if (dec_ctx) avcodec_close(dec_ctx);
  if (fmt_ctx) avformat_free_context(fmt_ctx);
}

bool InputFileSelectStream::eof()
{
  return eof_reached && decoded_frames.empty();
}

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
  // if (av_get_output_timestamp(fmt_ctx, stream_index, &dts, &wall) < 0)
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

  if (avformat_seek_file(fmt_ctx, stream_index, INT64_MIN, seek_timestamp, seek_timestamp, 0) < 0)
    throw ffmpegException("Could not seek to position " + std::to_string(val) + " s");
}

AVRational InputFileSelectStream::getFrameSAR(AVFrame *frame)
{
  return av_guess_sample_aspect_ratio(fmt_ctx, st, frame);
}

double InputFileSelectStream::getFrameTimeStamp(const AVFrame *frame)
{
  return av_q2d(st->time_base) * av_frame_get_best_effort_timestamp(frame);
}

///////////////////////////////

void InputFileSelectStream::open_file(const std::string &filename)
{
  if (filename.empty())
    throw ffmpegException("filename must be non-empty.");

  /* get default parameters from command line */
  fmt_ctx = avformat_alloc_context();
  if (!fmt_ctx)
    throw ffmpegException(filename, ENOMEM);

  fmt_ctx->flags |= AVFMT_FLAG_NONBLOCK;
  fmt_ctx->interrupt_callback = {NULL, NULL}; // from ffmpegBase

  ////////////////////

  AVDictionary *d = NULL;
  av_dict_set(&d, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);

  /* open the input file with generic avformat function */
  int err;
  if ((err = avformat_open_input(&fmt_ctx, filename.c_str(), NULL, &d)) < 0)
    throw ffmpegException(filename, err);
  
  if (d)
    av_dict_free(&d);

  /* If not enough info to get the stream parameters, we decode the
       first frames to get it. (used in mpeg case for example) */
  if (avformat_find_stream_info(fmt_ctx, NULL) < 0)
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

  int count = 0;
  for (int i = 0; i < (int)fmt_ctx->nb_streams; i++) // for each stream
  {
    // look for a stream which matches the
    if (fmt_ctx->streams[i]->codecpar->codec_type == type && count++ == index)
    {
      stream_index = i;
      st = fmt_ctx->streams[i];
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
      fmt_ctx->streams[i]->discard = AVDISCARD_ALL;
    }
  }
  if (!count)
    ffmpegException("Media file does not include the requested media type.");

  // set codec
  dec = avcodec_find_decoder(st->codecpar->codec_id);

  // create decoder context
  dec_ctx = avcodec_alloc_context3(dec);
  if (!dec_ctx)
    throw ffmpegException("Error allocating the decoder context.");

  // set the stream parameters to the decoder context
  if (avcodec_parameters_to_context(dec_ctx, st->codecpar) < 0)
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

// modified ffmpeg.c::static int init_input_stream(int ist_index, char *error, int error_len)
void InputFileSelectStream::init_stream()
{
  int ret;
  AVDictionary *decoder_opts = NULL;

  if (!dec)
    throw ffmpegException("Decoder (codec %s) not found for the input stream #%d", avcodec_get_name(dec_ctx->codec_id), st->index);

  dec_ctx->opaque = this;
  dec_ctx->get_format = get_format;
  dec_ctx->get_buffer2 = get_buffer;
  dec_ctx->thread_safe_callbacks = 1;

  // set pixel format to the desired
  // enum AVPixelFormat avcodec_find_best_pix_fmt_of_list(const enum AVPixelFormat *pix_fmt_list,
  //   enum AVPixelFormat src_pix_fmt,
  //   int has_alpha, int *loss_ptr);

  av_opt_set_int(dec_ctx, "refcounted_frames", 1, 0);
  if (dec_ctx->codec_id == AV_CODEC_ID_DVB_SUBTITLE)
  {
    av_dict_set(&decoder_opts, "compute_edt", "1", AV_DICT_DONT_OVERWRITE);
  }

  av_dict_set(&decoder_opts, "sub_text_format", "ass", AV_DICT_DONT_OVERWRITE);

  /* Useful for subtitles retiming by lavf (FIXME), skipping samples in
         * audio, and video decoders such as cuvid or mediacodec */
  av_codec_set_pkt_timebase(dec_ctx, st->time_base);

  if (!av_dict_get(decoder_opts, "threads", NULL, 0))
    av_dict_set(&decoder_opts, "threads", "auto", 0);
  if ((ret = avcodec_open2(dec_ctx, dec, &decoder_opts)) < 0)
  {
    if (ret == AVERROR_EXPERIMENTAL)
      throw ffmpegException("Error in an experimental decoder.");

    throw ffmpegException(ret);
    // snprintf(error, error_len,
    //          "Error while opening decoder for input stream "
    //          "#%d : %s",
    //          st->index, av_err2str(ret));
  }

  AVDictionaryEntry *t;
  if (t = av_dict_get(decoder_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))
  {
    throw ffmpegException("Option %s not found.\n", t->key);
  }

  // next_pts = AV_NOPTS_VALUE;
  // next_dts = AV_NOPTS_VALUE;
}

// static const HWAccel *InputFileSelectStream::get_hwaccel(enum AVPixelFormat pix_fmt)
// {
//     int i;
//     for (i = 0; hwaccels[i].name; i++)
//         if (hwaccels[i].pix_fmt == pix_fmt)
//             return &hwaccels[i];
//     return NULL;
// }

enum AVPixelFormat InputFileSelectStream::get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts)
{

  if (!pix_fmts)
    mexPrintf("Decoder's pixel format capability is unkown.");
  else
    for (const AVPixelFormat *p = pix_fmts; *p != -1; p++)
    {
      char buf[AV_FOURCC_MAX_STRING_SIZE];
      mexPrintf("%s\n", av_fourcc_make_string(buf, avcodec_pix_fmt_to_codec_tag(*p)));
    }

  return *pix_fmts;
  // InputFileSelectStream *ist = s->opaque;
  // const enum AVPixelFormat *p;
  // int ret;

  // for (p = pix_fmts; *p != -1; p++)
  // {
  //   const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
  //   const HWAccel *hwaccel;

  //   if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
  //     break;

  //   hwaccel = get_hwaccel(*p);
  //   if (!hwaccel ||
  //       (ist->active_hwaccel_id && ist->active_hwaccel_id != hwaccel->id) ||
  //       (ist->hwaccel_id != HWACCEL_AUTO && ist->hwaccel_id != hwaccel->id))
  //     continue;

  //   ret = hwaccel->init(s);
  //   if (ret < 0)
  //   {
  //     if (ist->hwaccel_id == hwaccel->id)
  //     {
  //       av_log(NULL, AV_LOG_FATAL,
  //              "%s hwaccel requested for input stream #%d:%d, "
  //              "but cannot be initialized.\n",
  //              hwaccel->name,
  //              ist->file_index, ist->st->index);
  //       return AV_PIX_FMT_NONE;
  //     }
  //     continue;
  //   }

  //   if (ist->hw_frames_ctx)
  //   {
  //     s->hw_frames_ctx = av_buffer_ref(ist->hw_frames_ctx);
  //     if (!s->hw_frames_ctx)
  //       return AV_PIX_FMT_NONE;
  //   }

  //   ist->active_hwaccel_id = hwaccel->id;
  //   ist->hwaccel_pix_fmt = *p;
  //   break;
  // }

  // return *p;
}

int InputFileSelectStream::get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
  // InputFileSelectStream *ist = s->opaque;

  // if (ist->hwaccel_get_buffer && frame->format == ist->hwaccel_pix_fmt)
  //   return ist->hwaccel_get_buffer(s, frame, flags);

  return avcodec_default_get_buffer2(s, frame, flags);
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
}

void InputFileSelectStream::free_thread(void)
{
  mexPrintf("Terminating threads.\n");

  kill_threads = true;
  suspend_threads = false;

  // filtered_frames.releaseAll();
  decoded_frames.releaseAll();
  raw_packets.releaseAll();

  suspend_cv.notify_all();

  // stop the threads
  if (read_thread.joinable())
    read_thread.join();
  if (decode_thread.joinable())
    decode_thread.join();
  // if (filter_thread.joinable()) filter_thread.join();

  // clear buffers
  // filtered_frames.flush(true);
  decoded_frames.flush(true);
  raw_packets.flush(true);
}

////////////////////////////////////////////////////////////////////////////////////////////////

void InputFileSelectStream::read_thread_fcn()
{
  read_state = 0;

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

      // mexPrintf("Reader [%d] Reserving an element in the packet queue (%d:%d).\n", ctr,raw_packets.elements(),raw_packets.available());
      AVPacket *pkt = raw_packets.get_container();
      if (!pkt || kill_threads)
        continue;

      // read next frame
      // mexPrintf("Reader [%d] Reading the next frame from file.\n", ctr);
      read_state = av_read_frame(fmt_ctx, pkt);

      // if any type of read error occurred, release the reserved queue element
      if (read_state < 0 || pkt->buf == NULL)
        raw_packets.send_cancel(pkt);

      if (read_state == AVERROR(EAGAIN)) // frame not ready, wait 10 ms
      {
        // mexPrintf("Reader [%d] Frame not ready; wait a little while and try again...\n", ctr);
        av_usleep(10000);
        continue;
      }

      // reached eof-of-file:
      if (read_state == AVERROR_EOF || pkt->buf == NULL)
      {
        if (loop < 0 || (loop--) > 0) // 0:no loop, >0: finite loop, <0: infinite loop
        {
          // mexPrintf("Reader [%d] EOF; Rewinding to loop...\n", ctr);
          // rewind
          read_state = av_seek_frame(fmt_ctx, -1, fmt_ctx->start_time, 0);
          // mexPrintf("Reader [%d] EOF; Releasing the reserved queue element...\n", ctr);

          // reset the counter
          frames_decoded = 0;
          samples_decoded = 0;
          pts = 0;
        }
        else // stop
        {
          // mexPrintf("Reader [%d] EOF; Stopping the thread\n", ctr);
          eof_reached = true;
          read_state = 0;
        }
        continue;
      }

      // unexcepted error occurred, terminate the thread
      if (read_state < 0)
      {
        // mexPrintf("Reader [%d] Unexpected error has occurred\n", ctr);
        break;
      }

      // if received packet is not of the stream, read next packet
      if (pkt->stream_index != stream_index)
      {
        // mexPrintf("Reader [%d] Frame is not for the selected stream\n", ctr);
        raw_packets.send_cancel(pkt);
        continue;
      }

      // place the new packet on the buffer
      // mexPrintf("Reader [%d] Finalize queueing of the frame (%d:%d).\n", ctr,raw_packets.elements(),raw_packets.available());
      raw_packets.send(pkt);

      // mexPrintf("Reader [%d] Packet is successfully placed on the queue\n", ctr++);
    }
  }
  catch (std::exception &e)
  {
    mexPrintf("Exception occurred in packet_reader thread: %s\n", e.what());
    throw e;
  }
}

void InputFileSelectStream::decode_thread_fcn()
{
  int ret;
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
      mexPrintf("Decoder [%d] Try to peek the next packet (%d:%d).\n", ctr, raw_packets.elements(), raw_packets.available());
      AVPacket *pkt = raw_packets.recv();
      mexPrintf("Decoder [%d] Peeking the next packet.\n", ctr);
      if (!pkt || suspend_threads || kill_threads)
        continue;

      // send packet to the decoder
      mexPrintf("Decoder [%d] Sending the packet to the FFmpeg decoder.\n", ctr);
      ret = avcodec_send_packet(dec_ctx, pkt);
      if (ret < 0)
      {
        mexPrintf("avcodec_send_packet failed with %d\n", ret);
        throw ffmpegException(ret);
      }
  
      // receive frames until there are no more
      int fctr = 0;
      while (ret != AVERROR(EAGAIN) && !(suspend_threads || kill_threads))
      {
        AVFrame **frame = decoded_frames.get_container();
        if (!frame || suspend_threads || kill_threads) continue;
      
        mexPrintf("Decoder [%d:%d] Receiving the decoded frame from FFmpeg decoder.\n", ctr, fctr);
        ret = avcodec_receive_frame(dec_ctx, *frame);
        if (suspend_threads || kill_threads || ret < 0) // if no frame decoded, cancel enqueue
        {
          decoded_frames.send_cancel(frame);
          if (suspend_threads || kill_threads)
            continue;
          if (ret == AVERROR(EAGAIN))
          {
            mexPrintf("Decoder [%d:%d] No more frames to be decoded for the current packet.\n", ctr, fctr);
            continue;
          }
          else
          {
            mexPrintf("Decoder [%d:%d] Something went wrong.\n", ctr, fctr);
            throw ffmpegException(av_err2str(ret));
          }
        }

        // update stats
        mexPrintf("Decoder [%d:%d] Update the frame count.\n", ctr, fctr);
        frames_decoded++;
        samples_decoded += (*frame)->nb_samples;

        //enqueue the newly decoded frame
        mexPrintf("Decoder [%d:%d] Releasing the buffer element.\n", ctr, fctr);
        decoded_frames.send(frame);
        
        mexPrintf("Decoder buffer: %d.\n", decoded_frames.elements());
        fctr++;
      }
      if (suspend_threads || kill_threads)
        continue;

      // release the packet (may not need to wait until the end of decoding...)
      mexPrintf("Decoder [%d] Releasing the packet element.\n", ctr);
      raw_packets.recv_done(pkt); // was_peeking=true
      
      ctr++;
    }
  }
  catch (std::exception &e)
  {
    mexPrintf("Exception occurred in decode_thread thread: %s\n", e.what());
    throw e;
  }
}

AVFrame *InputFileSelectStream::read_next_frame(const bool block)
{
  if (!block && decoded_frames.empty())
    return NULL;

  mexPrintf("read_next_frame(): waiting for the next frame to be decoded\n");
  AVFrame **recv_frame = decoded_frames.recv();
  AVFrame *frame = av_frame_alloc();
  mexPrintf("read_next_frame(): copying the decoded frame for caller's consumption\n");
  av_frame_ref(frame, *recv_frame);
  mexPrintf("read_next_frame(): mark the decoded frame consumed\n");
  decoded_frames.recv_done(recv_frame);

  return frame;
}

// void InputFileSelectStream::filter_audio()
//    // if decoded audio format is different from expected, configure a filter graph to resample
//    bool resample_changed = resample_sample_fmt != decoded_frame->format || resample_channels != dec_ctx->channels ||
//                       resample_channel_layout != decoded_frame->channel_layout || resample_sample_rate != decoded_frame->sample_rate;
//    if (resample_changed)
//    {
//       if (!guess_input_channel_layout(ist))
//          ffmpegException("Unable to find default channel layout\n");

//       decoded_frame->channel_layout = dec_ctx->channel_layout;

//       resample_sample_fmt = decoded_frame->format;
//       resample_sample_rate = decoded_frame->sample_rate;
//       resample_channel_layout = decoded_frame->channel_layout;
//       resample_channels = dec_ctx->channels;

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

//    if (avformat_seek_file(fmt_ctx, stream.index, INT64_MIN, seek_timestamp, seek_timestamp, 0) < 0)
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
