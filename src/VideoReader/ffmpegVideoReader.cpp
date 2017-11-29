#include <fstream>
#include <mutex>
static std::mutex lockfile;
static std::ofstream of("test.csv");
#define output(command)                     \
  \
{                                        \
    std::unique_lock<std::mutex>(lockfile); \
    of << command << std::endl;             \
  \
}

#include "ffmpegVideoReader.h"
#include "../Common/ffmpegException.h"
#include "../Common/ffmpegPtrs.h"
#include "../Common/ffmpegAvRedefine.h"

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavfilter/avfilter.h>
}

using namespace ffmpeg;

VideoReader::VideoReader(const std::string &filename, const std::string &filtdesc, const AVPixelFormat pix_fmt)
    : fmt_ctx(NULL), dec_ctx(NULL), filter_graph(NULL), buffersrc_ctx(NULL), buffersink_ctx(NULL),
      video_stream_index(-1), st(NULL), pix_fmt(AV_PIX_FMT_NONE), filter_descr(""),
      pts(0), eof(false), firstframe(NULL), buf(NULL),
      killnow(false), reader_status(IDLE), buffer_frames(true),
      eptr(NULL)
{
  if (!filename.empty())
    openFile(filename, filtdesc, pix_fmt);
}

VideoReader::~VideoReader()
{
  output("ffmpeg::VideoReader::destructor");
  closeFile();
}

////////////////////////////////////////////////////////////////////////////////////////////////

const AVPixFmtDescriptor &VideoReader::getPixFmtDescriptor() const
{
  const AVPixFmtDescriptor *pix_fmt_desc = av_pix_fmt_desc_get(pix_fmt);
  if (!pix_fmt_desc)
    ffmpegException("Pixel format is unknown.");
  return *pix_fmt_desc;
}

void VideoReader::open_input_file(const std::string &filename)
{
  int ret;
  AVCodec *dec;

  if (fmt_ctx)
    throw ffmpegException("Another file already open. Close it first.");

  if ((ret = avformat_open_input(&fmt_ctx, filename.c_str(), NULL, NULL)) < 0)
    throw ffmpegException("Cannot open input file");

  if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
    throw ffmpegException("Cannot find stream information");

  /* select the video stream */
  ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
  if (ret < 0)
    throw ffmpegException("Cannot find a video stream in the input file");
  video_stream_index = ret;
  st = fmt_ctx->streams[video_stream_index];

  // set to ignore all other streams
  for (int i = 0; i < (int)fmt_ctx->nb_streams; i++) // for each stream
    if (i != video_stream_index)
      fmt_ctx->streams[i]->discard = AVDISCARD_ALL;

  /* create decoding context */
  dec_ctx = avcodec_alloc_context3(dec);
  if (!dec_ctx)
    throw ffmpegException("Failed to allocate a decoder context");
  avcodec_parameters_to_context(dec_ctx, st->codecpar);
  av_opt_set_int(dec_ctx, "refcounted_frames", 1, 0);

  AVDictionary *decoder_opts = NULL;

  av_dict_set(&decoder_opts, "sub_text_format", "ass", AV_DICT_DONT_OVERWRITE);

  if (!av_dict_get(decoder_opts, "threads", NULL, 0))
    av_dict_set(&decoder_opts, "threads", "auto", 0);

  /* init the video decoder */
  if ((ret = avcodec_open2(dec_ctx, dec, &decoder_opts)) < 0)
    throw ffmpegException("Cannot open video decoder");
}

void VideoReader::close_input_file()
{
  av_frame_free(&firstframe);

  video_stream_index = -1;
  st = NULL;
  if (dec_ctx)
    avcodec_free_context(&dec_ctx);
  if (fmt_ctx)
    avformat_close_input(&fmt_ctx);
}

void VideoReader::destroy_filters()
{
  if (filter_graph)
    avfilter_graph_free(&filter_graph);
}

void VideoReader::create_filters(const std::string &filter_description, const AVPixelFormat pix_fmt_rq)
{

  // destroy existing filter graph
  if (!filter_graph)
    avfilter_graph_free(&filter_graph);

  // clear source and sink context pointers
  buffersrc_ctx = buffersink_ctx = NULL;

  // no filter if description not given
  if (filter_description.empty() && pix_fmt_rq==AV_PIX_FMT_NONE)
    return;

  if (!dec_ctx)
    throw ffmpegException("Decoder must be already open to create new filter graph.");

  int ret = 0;

  filter_graph = avfilter_graph_alloc();
  ffmpeg::AVFilterInOutPtr outputs(avfilter_inout_alloc(), ffmpeg::delete_filter_inout);
  ffmpeg::AVFilterInOutPtr inputs(avfilter_inout_alloc(), ffmpeg::delete_filter_inout);
  if (!outputs || !inputs || !filter_graph)
    throw ffmpegException("Failed to allocate the filter context or its AVFilterInOut's");

  /* buffer video source: the decoded frames from the decoder will be inserted here. */
  AVFilter *buffersrc = avfilter_get_by_name("buffer");
  char args[512];
  AVRational time_base = st->time_base;
  snprintf(args, sizeof(args),
           "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
           dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
           time_base.num, time_base.den,
           dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);
  ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
  if (ret < 0)
    throw ffmpegException("Cannot create buffer source: %s\n", av_err2str(ret));

  /* buffer video sink: to terminate the filter chain. */
  AVFilter *buffersink = avfilter_get_by_name("buffersink");
  ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
  if (ret < 0)
    throw ffmpegException("Cannot create buffer sink: %s", av_err2str(ret));

  AVPixelFormat pix_fmts[] = {pix_fmt_rq, AV_PIX_FMT_NONE};
  ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
  if (ret < 0)
    throw ffmpegException("Cannot set output pixel format: %s", av_err2str(ret));
  pix_fmt = pix_fmt_rq;

  /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

  /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
  outputs->name = av_strdup("in");
  outputs->filter_ctx = buffersrc_ctx;
  outputs->pad_idx = 0;
  outputs->next = NULL;

  /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
  inputs->name = av_strdup("out");
  inputs->filter_ctx = buffersink_ctx;
  inputs->pad_idx = 0;
  inputs->next = NULL;

  if (filter_description.size()) // false if only format change
  {
    AVFilterInOut *in = inputs.release();
    AVFilterInOut *out = outputs.release();
    if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_description.c_str(), &in, &out, NULL)) < 0)
      throw ffmpegException("filtering_video:create_filters:avfilter_graph_parse_ptr:error: %s", av_err2str(ret));
    inputs.reset(in);
    outputs.reset(out);
    filter_descr = filter_description;
  }

  if (ret = avfilter_graph_config(filter_graph, NULL))
    throw ffmpegException("filtering_video:create_filters:avfilter_graph_config:error: %s", av_err2str(ret));
}

void VideoReader::setCurrentTimeStamp(const double val)
{
  if (!isFileOpen())
    throw ffmpegException("No file open.");

output("Pausing threads");
  // pause the threads and flush the remaining frames
  pause();

output("Threads paused. Resetting timestamp");
  // set new time
  int64_t seek_timestamp = int64_t(val * AV_TIME_BASE);

  if (!(fmt_ctx->iformat->flags & AVFMT_SEEK_TO_PTS) && st->codecpar->video_delay)
    seek_timestamp -= 3 * AV_TIME_BASE / 23;

  if (avformat_seek_file(fmt_ctx, video_stream_index, INT64_MIN, seek_timestamp, seek_timestamp, 0) < 0)
    throw ffmpegException("Could not seek to position " + std::to_string(val) + " s");

  // restart
output("Timestamp reset. Resuming threads");
  resume();
output("Threads active");

}

/////////////////////////////////////////////////////////////////////////////////////////////
// THREAD FUNCTIONS: Read file and send it to the FFmpeg decoder

void VideoReader::start()
{
  killnow = false;

  // start the file reading thread (sets up and idles)
  packet_reader = std::thread(&VideoReader::read_packets, this);
  frame_filter = std::thread(&VideoReader::filter_frames, this);
  
  //start reading immediately
  resume();
  
  // wait until the first frame is ready
  std::unique_lock<std::mutex> firstframe_guard(firstframe_lock);
  firstframe_ready.wait(firstframe_guard, [&]() { return killnow || eof || firstframe; });

  av_log(dec_ctx, AV_LOG_INFO, "frame[%d]:width=%d,height=%d,format=%s,key_frame=%d,pict_type=%c,SAR=%d/%d,pts=%d,display_picture_number=%d\n",
         firstframe->best_effort_timestamp, firstframe->width, firstframe->height, av_get_pix_fmt_name((AVPixelFormat)firstframe->format),
         firstframe->key_frame, av_get_picture_type_char(firstframe->pict_type), firstframe->sample_aspect_ratio.num, firstframe->sample_aspect_ratio.den,
         firstframe->pts, firstframe->display_picture_number);
  av_log(dec_ctx, AV_LOG_INFO, "frame[%d]:repeat_pict=%d',interlaced_frame=%d,top_field_first=%d,top_field_first=%d, palette_has_changed=%d\n",
         firstframe->best_effort_timestamp, firstframe->repeat_pict, firstframe->interlaced_frame, firstframe->top_field_first, firstframe->palette_has_changed);
  for (int i = 0; firstframe->linesize[i]; ++i) // for each planar component
    av_log(dec_ctx, AV_LOG_INFO, "frame[%d]:plane[%d]:linesize=%d\n", firstframe->best_effort_timestamp, i, firstframe->linesize[i]);
}

void VideoReader::pause()
{
  // already idle
  if (reader_status == IDLE)
    return;

  buffer_frames = false; // don't block even if buffer full, drop remaining blocks
  reader_status = PAUSE_RQ;
  output("Reader status set to PAUSE_RQ");

  // set to flush pipeline
  std::unique_lock<std::mutex> buffer_guard(buffer_lock);
output("Turned off BUFFER_FRAMES FLAG");
  buffer_ready.notify_all();

  // wait until all remaining frames are processed
  if (!buffer_frames)
    buffer_flushed.wait(buffer_guard);
}

void VideoReader::resume()
{
  std::unique_lock<std::mutex> reader_guard(reader_lock);
  reader_status = ACTIVE;
  reader_ready.notify_all();
}

void VideoReader::stop()
{
  // make sure no thread is stuck on a wait state
  killnow = true;

  {
    std::unique_lock<std::mutex> reader_guard(reader_lock);
    reader_ready.notify_all();
  }
  {
    std::unique_lock<std::mutex> decoder_guard(decoder_lock);
    decoder_ready.notify_all();
  }
  {
    std::unique_lock<std::mutex> buffer_guard(buffer_lock);
    buffer_ready.notify_all();
  }

  // start the file reading thread (sets up and idles)
  if (packet_reader.joinable())
    packet_reader.join();
  if (frame_filter.joinable())
    frame_filter.join();
}

void VideoReader::read_packets()
{
  int ret;
  AVPacket packet;
  bool last_frame = false;

  try
  {
    // initialize to allow preemptive unreferencing
    av_init_packet(&packet);

    /* read all packets */
    while (!killnow)
    {
      // wait until a file is opened and the reader is unleashed
      if (reader_status == IDLE)
      {
        std::unique_lock<std::mutex> reader_guard(reader_lock);
        reader_ready.wait(reader_guard);
        reader_guard.unlock();
        if (killnow)
          break;
      }

      if (reader_status == PAUSE_RQ)
      {
        output("read_packets::ACK PAUSE_RQ");
        last_frame = true;
      }
      else
      {
        /* read next packet packets */
        av_packet_unref(&packet);
        if ((ret = av_read_frame(fmt_ctx, &packet)) < 0)
        {
          if (ret == AVERROR_EOF) // reached end of the file
            last_frame = true;
          else
            throw ffmpegException("Error while reading a packet: %s", av_err2str(ret));
        }

        // process only the video stream
        if (packet.stream_index != video_stream_index)
          continue;
      }

      /* send packet to the decoder */
      // decoder input buffer is full, wait until space available
      std::unique_lock<std::mutex> decoder_guard(decoder_lock);
      ret = avcodec_send_packet(dec_ctx, last_frame ? NULL : &packet);
      output("read_packet()::sending_packet <= " << ret);
      while (!killnow && (ret == AVERROR(EAGAIN)))
      {
        decoder_ready.wait(decoder_guard);
        if (killnow)
        {
          output("read_packet()::quit_sending_packet::killnow set");
          break;
        }
        ret = avcodec_send_packet(dec_ctx, last_frame ? NULL : &packet);
        output("read_packet()::resending_packet <= " << ret);
      }
      decoder_ready.notify_one(); // notify the frame_filter thread for the availability
      decoder_guard.unlock();
      if (killnow)
        break;
      if (ret < 0 && ret != AVERROR_EOF)
        throw ffmpegException("Error while sending a packet to the decoder: %s", av_err2str(ret));

      // if just completed EOF flushing, done
      if (last_frame)
        reader_status = IDLE;
    }
  }
  catch (const std::exception &e)
  {
    output("read_packets()::failed::" << e.what());
    // log the exception
    eptr = std::current_exception();

    // flag the exception
    killnow = true;
    reader_status = FAILED;
    reader_ready.notify_all();
    decoder_ready.notify_all();
    buffer_flushed.notify_all();
  }
  output("read_packets()::terminated");
}

void VideoReader::filter_frames()
{
  int ret;
  AVFrame *frame = av_frame_alloc();
  AVFrame *filt_frame = av_frame_alloc();
  bool last_frame = false;
  try
  {
    /* read all packets */
    while (!killnow)
    {
      std::unique_lock<std::mutex> decoder_guard(decoder_lock);
      ret = avcodec_receive_frame(dec_ctx, frame);
      output("filter_frames()::trying_receiving_frame => " << ret);
      while (!killnow && (ret == AVERROR(EAGAIN)))
      { /* receive decoded frames from decoder (wait until there is one available) */
        decoder_ready.wait(decoder_guard);
        ret = avcodec_receive_frame(dec_ctx, frame);
        output("filter_frames()::trying_re-receiving_frame => " << ret);
      }
      decoder_ready.notify_one(); // notify the packet_reader thread that decoder may be available now
      decoder_guard.unlock();

      if (killnow)
        break;
      if (ret == AVERROR_EOF)
        last_frame = true;
      else if (ret < 0)
        throw ffmpegException("Error while receiving a frame from the decoder: %s", av_err2str(ret));
      else
        frame->pts = av_frame_get_best_effort_timestamp(frame);

      if (filter_graph)
      {
        /* pull filtered frames from the filtergraph */
        ret = av_buffersrc_add_frame_flags(buffersrc_ctx, last_frame ? NULL : frame, AV_BUFFERSRC_FLAG_KEEP_REF);
        if (ret<0)
          throw ffmpegException("Error occurred while sending a frame to the filter graph: %s", av_err2str(ret));

        // try to retrieve the buffer
        ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
        output("filter_frames()::trying_receiving_filtered_frame => " << ret);
        while (!killnow && (ret >= 0))
        {
          copy_frame_ts(filt_frame, buffersink_ctx->inputs[0]->time_base, buffer_frames);
          av_frame_unref(filt_frame);
          if (killnow)
            break;
          ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
          output("filter_frames()::trying_re-receiving_filtered_frame => " << ret);
        }

        if (ret == AVERROR_EOF) // run copy_frame_ts one last time if EOF to let buffer know
          copy_frame_ts(NULL, buffersink_ctx->inputs[0]->time_base, buffer_frames);
        else if (!killnow && ret < 0 && ret!=AVERROR(EAGAIN))
          throw ffmpegException("Error occurred while retrieving filtered frames: %s", av_err2str(ret));
      }
      else
      {
        // no filtering, buffer immediately
        copy_frame_ts(last_frame ? NULL : frame, st->time_base, buffer_frames);
      }

      // save the timestamp as the last buffered
      if (frame)
        pts = frame->pts;

      // clear last_frame flag -or- release frame
      if (last_frame)
      {
        last_frame = false;
        
        if (!buffer_frames) // finished flushing, reset buffering flag
        {
          buffer_frames = true;
          buffer_flushed.notify_one();
        }
      }
      else if (ret != AVERROR(EAGAIN))
      {
        av_frame_unref(frame);
      }
    }
  }
  catch (const std::exception &e)
  {
    // log the exception
    eptr = std::current_exception();

    output("filter_frames()::failed::" << e.what());

    // flag the exception
    killnow = true;
    reader_ready.notify_all();
    decoder_ready.notify_all();
    buffer_flushed.notify_all();
  }

  // release the frames
  av_frame_free(&frame);
  av_frame_free(&filt_frame);

  output("filter_frames()::terminated");
}

int VideoReader::copy_frame_ts(const AVFrame *frame, AVRational time_base, bool block)
{
  // keep the first frame as the reference
  if (!firstframe)
  {
    output("copy_frame_ts()::saving_first_frame");
    std::unique_lock<std::mutex> firstframe_guard(firstframe_lock);
    if (frame)
      firstframe = av_frame_clone(frame);
    output("copy_frame_ts()::first_frame_saved");
    firstframe_ready.notify_one();
  }

  std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  // if null, reached the end-of-file (or stopped by user command)
  // copy frame to buffer; if buffer not ready, wait until it is
  int ret = (buf) ? buf->copy_frame(frame, time_base) : AVERROR(EAGAIN);
  output("copy_frame_ts()::copying_frame => " << ret);
  while (block && !killnow && (ret == AVERROR(EAGAIN)))
  {
     buffer_ready.wait(buffer_guard);
    if (killnow)
      break;
    ret = (buf) ? buf->copy_frame(frame, time_base) : AVERROR(EAGAIN);
    output("copy_frame_ts()::re-copying_frame => " << ret);
  }
  if (!killnow)
    output("copy_frame_ts()::frame_copied");

  if (!ret) // if successfully buffered
    buffer_ready.notify_one();

  return ret;
}

////////////////////////////////////////////

bool VideoReader::isFileOpen()
{
  // decoder context must be open
  if (!dec_ctx)
    return false;

  // check for thread failure
  if (killnow) // normally only true if file is closed
  {
    try
    {
      if (eptr)
      {
        std::rethrow_exception(eptr);
      }
    }
    catch (...)
    {
      throw;
    }
  }

  // all threads must be running
  return true;
}

void VideoReader::resetBuffer(FrameBuffer *new_buf)
{
  output("resetBuffer():begin");
  std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  buf = new_buf;
  output("resetBuffer():new buffer set");
  buffer_ready.notify_one();
  output("resetBuffer():notified");
}

FrameBuffer* VideoReader::releaseBuffer()
{
  FrameBuffer *rval = buf;
  resetBuffer(NULL);
  return rval;
}

size_t VideoReader::blockTillBufferFull()
{
  if (!isFileOpen() || !buf)
    return 0;
  std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  buffer_ready.wait(buffer_guard, [&]() { return killnow || reader_status == IDLE || buf->full(); });
  return buf->size();
}

size_t VideoReader::blockTillFrameAvail(size_t min_cnt)
{
  if (!isFileOpen() || !buf)
    return 0;
  std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  buffer_ready.wait(buffer_guard, [&]() { return killnow || reader_status == IDLE || buf->available() >= min_cnt; });
  return buf->available();
}
