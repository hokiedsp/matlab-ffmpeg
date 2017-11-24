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

#include <fstream>
static std::mutex lockfile;
static std::ofstream of("test.csv");
#define output(command)                     \
  \
{                                        \
    std::unique_lock<std::mutex>(lockfile); \
    of << command << std::endl;             \
  \
}

using namespace ffmpeg;

VideoReader::VideoReader(const std::string &filename, const std::string &filtdesc, const AVPixelFormat pix_fmt)
    : fmt_ctx(NULL), dec_ctx(NULL), filter_graph(NULL), buffersrc_ctx(NULL), buffersink_ctx(NULL),
      video_stream_index(-1), st(NULL), pix_fmt(AV_PIX_FMT_NONE), filter_descr(""),
      pts(0), eof(false), paused(false), firstframe(NULL),
      buf_size(0), frame_buf(NULL), time_buf(NULL), buf_count(0),
      killnow(false), reader_status(IDLE),
      eptr(NULL)
{
  output("VideoReader::VideoReader::pre-open");
  if (!filename.empty())
    openFile(filename, filtdesc, pix_fmt);
  output("VideoReader::VideoReader::post-open");
}

VideoReader::~VideoReader()
{
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
  if (filter_description.empty())
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

  AVFilterInOut *in = inputs.release();
  AVFilterInOut *out = outputs.release();
  if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_description.c_str(), &in, &out, NULL)) < 0)
    throw ffmpegException("filtering_video:create_filters:error: %s", av_err2str(ret));
  inputs.reset(in);
  outputs.reset(out);
  filter_descr = filter_description;

  if (ret = avfilter_graph_config(filter_graph, NULL))
    throw ffmpegException("filtering_video:create_filters:error: %s", av_err2str(ret));
}

/////////////////////////////////////////////////////////////////////////////////////////////
// THREAD FUNCTIONS: Read file and send it to the FFmpeg decoder

void VideoReader::start()
{
  killnow = false;

  // start the file reading thread (sets up and idles)
  packet_reader = std::thread(&VideoReader::read_packets, this);
  frame_filter = std::thread(&VideoReader::filter_frames, this);
  frame_output = std::thread(&VideoReader::buffer_frames, this);

  // wait until the first frame is ready
  output("firstframe: " << (bool)firstframe);
  std::unique_lock<std::mutex> firstframe_guard(firstframe_lock);
  firstframe_ready.wait(firstframe_guard, [&]() { return killnow || firstframe; });

  output("killnow: " << killnow);
  output("firstframe: " << (bool)firstframe);
  
}

void VideoReader::stop()
{
  // make sure no thread is stuck on a wait state
  killnow = true;
  decoder_input_ready.notify_one();
  decoder_output_ready.notify_one();
  filter_input_ready.notify_one();
  filter_output_ready.notify_one();
  buffer_ready.notify_one();

  // start the file reading thread (sets up and idles)
  if (packet_reader.joinable())
    packet_reader.join();
  if (frame_filter.joinable())
    frame_filter.join();
  if (frame_output.joinable())
    frame_output.join();
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
    while (!killnow && reader_status != FAILED)
    {
      // wait until a file is opened and the reader is unleashed
      std::unique_lock<std::mutex> reader_guard(reader_lock);
      reader_ready.wait(reader_guard, [&]() { return (killnow || reader_status != IDLE); });
      reader_guard.unlock();

      if (reader_status == STOP)
      {
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
      }

      /* send packet to the decoder */
      // decoder input buffer is full, wait until space available
      std::unique_lock<std::mutex> decoder_guard(decoder_input_lock);
      decoder_input_ready.wait(decoder_guard, [&]() {
        if (killnow)
          return true;
        ret = avcodec_send_packet(dec_ctx, last_frame ? NULL : &packet);
      output("read_packet()::sending_packet => " << ret);
        return ret != AVERROR(EAGAIN);
      });
      if (killnow)
        break;
      if (ret < 0)
        throw ffmpegException("Error while sending a packet to the decoder: %s", av_err2str(ret));
output("read_packet()::sent_packet");
      
      // notify the frame_filter thread for the availability
      decoder_output_ready.notify_one();

      // if just completed EOF flushing, done
      if (last_frame)
        reader_status = IDLE;
    }
  }
  catch (const std::exception& e)
  {
    output("read_packets()::failed::" << e.what());
    // log the exception
    eptr = std::current_exception();

    // flag the exception
    killnow = true;
    reader_status = FAILED;
  }
}

void VideoReader::filter_frames()
{
  int ret;
  AVFrame *frame = av_frame_alloc();
  bool last_frame = false;

  try
  {
    /* read all packets */
    while (!killnow)
    {
      output("filter_frames()::receive_frame");
      { /* receive decoded frames from decoder (wait until there is one available) */
        std::unique_lock<std::mutex> decoder_guard(decoder_output_lock);
        decoder_output_ready.wait(decoder_guard, [&]() {
          if (killnow)
            return true;
          ret = avcodec_receive_frame(dec_ctx, frame);
output("filter_frames()::trying_receiving_frame => " << ret);
          return (ret != AVERROR(EAGAIN));
        });
      }

      // notify the packet_reader thread that decoder input may be available now
      decoder_input_ready.notify_one();

      if (killnow)
        break;
      if (ret == AVERROR_EOF)
        last_frame = true;
      else if (ret < 0)
        throw ffmpegException("filtering_video:error", "Error while receiving a frame from the decoder: %s", av_err2str(ret));
      else
        frame->pts = av_frame_get_best_effort_timestamp(frame);

      if (filter_graph)
      {
output("filter_frames()::sending_frame");
        /* push the decoded frame into the filtergraph */
        std::unique_lock<std::mutex> filter_guard(filter_input_lock);
        filter_input_ready.wait(filter_guard, [&]() {
          if (killnow)
            return true;
output("filter_frames()::try_sending_frame...");
          ret = av_buffersrc_add_frame_flags(buffersrc_ctx, last_frame ? NULL : frame, AV_BUFFERSRC_FLAG_KEEP_REF);
          if (eof)
            last_frame = false;
output("filter_frames()::try_sending_frame => " << ret);
          return ret != AVERROR(EAGAIN);
        });
        if (killnow)
          break;
        if (ret < 0)
          throw ffmpegException("filtering_video:error", "Error while feeding the filtergraph: %s", av_err2str(ret));

        // notify the frame_filter thread for the availability
        filter_output_ready.notify_one();

        av_frame_unref(frame);

      }
      else
        copy_frame_ts(frame, st->time_base);
    }
  }
  catch (const std::exception& e)
  {
    // log the exception
    eptr = std::current_exception();

    output("filter_frames()::failed::" << e.what());

    // flag the exception
    killnow = true;
  }

  // release the frames
  av_frame_free(&frame);
}

void VideoReader::buffer_frames()
{
  int ret;
  AVFrame *filt_frame = av_frame_alloc();

  try
  {
    /* read all packets */
    while (!killnow) // thread persists for the life span of the class instance
    {
      output("buffer_frames()::get_frame_from_filter");
      // receive the next filtered frame from the filter graph
      std::unique_lock<std::mutex> filter_guard(filter_output_lock);
      filter_output_ready.wait(filter_guard, [&]() {
        if (killnow)
          return true;
      output("buffer_frames()::try_getting_frame_from_filter...");
        ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
      output("buffer_frames()::try_getting_frame_from_filter => " << ret);
        return ret != AVERROR(EAGAIN);
      });
      filter_input_ready.notify_one();

      if (ret == AVERROR_EOF) // reached the end of file -or- reader stopped
      {
        eof = true;
      }
      else if (ret < 0)
        throw ffmpegException("buffer_frames:error", "Error occurred: %s", av_err2str(ret));
      else // successfully retrieved a filtered frame
      {
        // place the new output frame to the user supplied buffer
        // * this thread may need to wait user to supply a buffer
        // *
        output("buffer_frames()::place_frame_in_buffer");
        copy_frame_ts(filt_frame, buffersink_ctx->inputs[0]->time_base);

        // release the output frame
        av_frame_unref(filt_frame);
      }
    }
  }
  catch (const std::exception& e)
  {
    // log the exception
    eptr = std::current_exception();

    // flag the exception
    killnow = true;

    output("buffer_frames()::failed::" << e.what());
  }

  // release the frames
  av_frame_free(&filt_frame);
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

size_t VideoReader::resetBuffer(size_t sz, uint8_t *frame[], double *time)
{
  std::unique_lock<std::mutex> buffer_guard(buffer_lock);

  size_t rval = buf_size;

  buf_size = sz;
  frame_buf = frame;
  time_buf = time;
  buf_count = 0;

  buffer_ready.notify_one();

  return rval; // return the current buffer size
}

size_t VideoReader::releaseBuffer()
{
  return resetBuffer(0, NULL, NULL);
}

size_t VideoReader::blockTillBufferFull()
{
  if (!isFileOpen())
    return 0;
  std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  buffer_ready.wait(buffer_guard, [&]() { return killnow || reader_status == IDLE || buf_count == buf_size; });
  return buf_count;
}

size_t VideoReader::blockTillFrameAvail(size_t min_cnt)
{
  if (!isFileOpen())
    return 0;
  std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  buffer_ready.wait(buffer_guard, [&]() { return killnow || reader_status == IDLE || buf_count >= min_cnt; });
  return buf_count;
}

void VideoReader::copy_frame_ts(const AVFrame *frame, AVRational time_base)
{
  // keep the first frame as the reference
  if (!firstframe)
  {
    output("copy_frame_ts()::saving_first_frame");
    std::unique_lock<std::mutex> firstframe_guard(firstframe_lock);
    firstframe = av_frame_clone(frame);
    output("copy_frame_ts()::first_frame_saved");
    firstframe_ready.notify_one();
  }

  // copy frame to buffer; if buffer not ready, wait until it is
  output("copy_frame_ts()::copying_frame");
  std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  buffer_ready.wait(buffer_guard, [&]() {
    if (killnow)
      return true;
    return (copy_frame(frame, buffersink_ctx->inputs[0]->time_base) != AVERROR(EAGAIN));
  });
  output("copy_frame_ts()::frame_copyed");
  
  // notify the buffer state has changed
  buffer_ready.notify_one();
}

int VideoReader::copy_frame(const AVFrame *frame, AVRational time_base)
{
  // expects having exclusive access to the user supplied buffer

  if (!buf_size || buf_count == buf_size) // receiving data buffer not set
    return AVERROR(EAGAIN);

  // increase the counter of frames filled in the output buffer
  ++buf_count;

  // copy time
  pts = frame->pts;
  if (time_buf)
  {
    if (frame->pts != AV_NOPTS_VALUE)
      *time_buf = double(frame->pts / 100) / (AV_TIME_BASE / 100);
    time_buf++;
  }

  int Npx = frame->height * frame->width;

  for (int i = 0; i < getNbPlanar(); ++i) // for each planar component
  {
    output("Plane[" << i << "]: linesize=" << frame->linesize[i]);
    // // Copy frame data
    // if (frame->width == frame->linesize[i])
    // {
    //   std::copy_n(frame->data[i], Npx, frame_buf[i]);
    // }
    // else
    // {
    //   uint8_t *srcdata = frame->data[i];
    //   uint8_t *dstdata = frame_buf[i];
    //   int src_lsz = frame->linesize[i];
    //   int dst_lsz = frame->width;
    //   if (src_lsz < 0)
    //     srcdata -= src_lsz * (frame->height - 1);
    //   for (int h = 0; h < frame->height; h++)
    //   {
    //     dstdata = std::copy_n(srcdata, dst_lsz, dstdata);
    //     srcdata += src_lsz;
    //   }
    // }
    // frame_buf[i] += Npx;
  }
  return 0;
}
