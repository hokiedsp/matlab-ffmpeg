#include "ffmpegVideoReader.h"
#include "../Common/ffmpegException.h"
#include "../Common/ffmpegPtrs.h"

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavfilter/avfilter.h>
}

// #include <fstream>
// static std::mutex lockfile;
// static std::ofstream of("test.csv");
// #define output(command)                     \
//   \
// {                                        \
//     std::unique_lock<std::mutex>(lockfile); \
//     of << command << std::endl;             \
//   \
// }

using namespace ffmpeg;

VideoReader::VideoReader() : fmt_ctx(NULL), dec_ctx(NULL),
                             filter_graph(avfilter_graph_alloc()), video_stream_index(-1),
                             buf_size(0), frame_buf(NULL), time_buf(NULL),
                             killnow(false), reader_status(INIT), eptr(NULL)
{
  // start the file reading thread (sets up and idles)
  packet_reader = std::thread(&VideoReader::read_packets, this);
  frame_filter = std::thread(&VideoReader::filter_frames, this);
  frame_output = std::thread(&VideoReader::buffer_frames, this);
}

VideoReader::~VideoReader()
{
  killnow = true;

  if (dec_ctx)
    avcodec_free_context(&dec_ctx);
  if (fmt_ctx)
    avformat_close_input(&fmt_ctx);
  if (filter_graph)
    avfilter_graph_free(&filter_graph);
}

void VideoReader::close_input_file()
{
}

void VideoReader::open_input_file(const std::string &filename)
{
  int ret;
  AVCodec *dec;

  if ((ret = avformat_open_input(&fmt_ctx, filename.c_str(), NULL, NULL)) < 0)
    throw ffmpegException("Cannot open input file");

  if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
    throw ffmpegException("Cannot find stream information");

  /* select the video stream */
  ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
  if (ret < 0)
    throw ffmpegException("Cannot find a video stream in the input file");
  video_stream_index = ret;

  // set to ignore all other streams
  for (int i = 0; i < (int)fmt_ctx->nb_streams; i++) // for each stream
    if (i != video_stream_index)
      fmt_ctx->streams[i]->discard = AVDISCARD_ALL;

  /* create decoding context */
  dec_ctx = avcodec_alloc_context3(dec);
  if (!dec_ctx)
    throw ffmpegException("Failed to allocate a decoder context");
  avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[video_stream_index]->codecpar);
  av_opt_set_int(dec_ctx, "refcounted_frames", 1, 0);

  AVDictionary *decoder_opts = NULL;

  av_dict_set(&decoder_opts, "sub_text_format", "ass", AV_DICT_DONT_OVERWRITE);

  if (!av_dict_get(decoder_opts, "threads", NULL, 0))
    av_dict_set(&decoder_opts, "threads", "auto", 0);

  /* init the video decoder */
  if ((ret = avcodec_open2(dec_ctx, dec, &decoder_opts)) < 0)
    throw ffmpegException("Cannot open video decoder");
}

void VideoReader::init_filters(const std::string &filter_description, const AVPixelFormat pix_fmt)
{
  char args[512];
  int ret = 0;
  AVFilter *buffersrc = avfilter_get_by_name("buffer");
  AVFilter *buffersink = avfilter_get_by_name("buffersink");
  ffmpeg::AVFilterInOutPtr outputs(avfilter_inout_alloc(), ffmpeg::delete_filter_inout);
  ffmpeg::AVFilterInOutPtr inputs(avfilter_inout_alloc(), ffmpeg::delete_filter_inout);
  AVRational time_base = fmt_ctx->streams[video_stream_index]->time_base;
  AVPixelFormat pix_fmts[] = {pix_fmt, AV_PIX_FMT_NONE};

  if (!outputs || !inputs || !filter_graph)
    throw ffmpegException("Failed to allocate the filter context or its AVFilterInOut's");

  /* buffer video source: the decoded frames from the decoder will be inserted here. */
  snprintf(args, sizeof(args),
           "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
           dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
           time_base.num, time_base.den,
           dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);
  ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
  if (ret < 0)
    throw ffmpegException("Cannot create buffer source: %s", av_err2str(ret));

  /* buffer video sink: to terminate the filter chain. */
  ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
  if (ret < 0)
    throw ffmpegException("Cannot create buffer sink: %s", av_err2str(ret));

  ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
  if (ret < 0)
    throw ffmpegException("Cannot set output pixel format: %s", av_err2str(ret));

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

  // append transpose filter at the end to show the output in the proper orientation in MATLAB
  std::string tr_filter_descr("transpose=dir=0");
  if (filter_descr.size())
    tr_filter_descr = "," + tr_filter_descr;

  // output("filter string: " << (filter_descr + tr_filter_descr).c_str());

  AVFilterInOut *in = inputs.release();
  AVFilterInOut *out = outputs.release();
  if ((ret = avfilter_graphs_parse_ptr(filter_graph, (filter_descr + tr_filter_descr).c_str(),
                                       &in, &out, NULL)) < 0)
    throw ffmpegException("filtering_video:init_filters:error", "%s", av_err2str(ret));
  inputs.reset(in);
  outputs.reset(out);

  if (ret = avfilter_graph_config(filter_graph, NULL))
    throw ffmpegException("filtering_video:init_filters:error", "%s", av_err2str(ret));
}

// THREAD FUNCTIONS: Read file and send it to the FFmpeg decoder
void VideoReader::read_packets()
{
  int ret;
  AVPacket packet;

  try
  {
    // initialize to allow preemptive unreferencing
    av_init_packet(&packet);

    /* read all packets */
    std::unique_lock<std::mutex> reader_guard(lock_reader, std::defer_lock);
    while (!killnow && reader_status != FAILED)
    {

      if (reader_status == IDLE) // idle
      {
        reader_guard.lock();
        reader_start.wait(reader_guard, [&]() { return killnow || reader_status == ACTIVE; });
        reader_guard.unlock();
      }

      /* read next packet packets */
      if ((ret = av_read_frame(fmt_ctx, &packet)) < 0)
      {
        if (ret == AVERROR_EOF) // reached end of the file
          reader_status = FLUSH;
        else
          throw ffmpegException("Error while reading a packet: %s", av_err2str(ret));
      }

      /* send packet to the decoder */
      if (reader_status == FLUSH)
      {
        ret = avcodec_send_packet(dec_ctx, NULL);
        reader_status = IDLE;
      }
      else if (packet.stream_index == video_stream_index)
      {
        ret = avcodec_send_packet(dec_ctx, &packet);
        av_packet_unref(&packet);
      }
      if (ret < 0)
        throw ffmpegException("Error while sending a packet to the decoder: %s", av_err2str(ret));
    }
  }
  catch (...)
  {
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

  try
  {
    /* read all packets */
    std::unique_lock<std::mutex> filter_guard(lock_filter, std::defer_lock);
    while (!killnow)
    {

      if (filter_status == IDLE) // idle
      {
        filter_guard.lock();
        filter_start.wait(filter_guard, [&]() { return killnow || filter_status == ACTIVE; });
        filter_guard.unlock();
      }

      /* receive decoded frames from decoder */
      ret = avcodec_receive_frame(dec_ctx, frame);
      if (ret == AVERROR(EAGAIN))
      {
        filter_status = IDLE;
        continue;
      }
      else if (ret == AVERROR_EOF)
      {
        filter_status = FLUSH;
      }
      else if (ret < 0)
        throw ffmpegException("filtering_video:error", "Error while receiving a frame from the decoder: %s", av_err2str(ret));
      else
        frame->pts = av_frame_get_best_effort_timestamp(frame);

      /* push the decoded frame into the filtergraph */
      if (av_buffersrc_add_frame_flags(buffersrc_ctx, (filter_status = FLUSH) ? NULL : frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0)
        throw ffmpegException("filtering_video:error", "Error while feeding the filtergraph: %s", av_err2str(ret));

      av_frame_unref(frame);
    }
  }
  catch (...)
  {
    // log the exception
    eptr = std::current_exception();

    // flag the exception
    killnow = true;
    filter_status = FAILED;
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
    std::unique_lock<std::mutex> outputter_guard(lock_outputter, std::defer_lock);
    while (!killnow)
    {

      if (buffer_status == IDLE) // idle
      {
        outputter_guard.lock();
        outputter_start.wait(outputter_guard, [&]() { return killnow || buffer_status == ACTIVE; });
        outputter_guard.unlock();
      }

      ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
      if (ret == AVERROR(EAGAIN))
      {
        buffer_status = IDLE; // wait until next frame is ready
      }
      else if (ret == AVERROR_EOF)
      {
      }
      if (ret < 0)
        throw ffmpegException("filtering_video:error", "Error occurred: %s", av_err2str(ret));

      if (!frame_buf && !time_buf) // receiving data buffer not set
      {
      }

      copy_frame(filt_frame, buffersink_ctx->inputs[0]->time_base);
      av_frame_unref(filt_frame);
    }
  }
  catch (...)
  {
    // log the exception
    eptr = std::current_exception();

    // flag the exception
    killnow = true;
    buffer_status = FAILED;
  }

  // release the frames
  av_frame_free(&filt_frame);
}

void VideoReader::copy_frame(const AVFrame *frame, AVRational time_base)
{
  // copy time
  if (time_buf)
  {
    if (frame->pts != AV_NOPTS_VALUE)
      *time_buf = double(frame->pts / 100) / (AV_TIME_BASE / 100);
    time_buf++;
  }

  int Npx = frame->height * frame->width;

  // Copy frame data
  if (frame->width == frame->linesize[0])
  {
    std::copy_n(frame->data[0], Npx, frame_buf);
  }
  else
  {
    uint8_t *srcdata = frame->data[0];
    uint8_t *dstdata = frame_buf;
    int src_lsz = frame->linesize[0];
    int dst_lsz = frame->width;
    if (src_lsz < 0)
      srcdata -= src_lsz * (frame->height - 1);
    for (int h = 0; h < frame->height; h++)
    {
      dstdata = std::copy_n(srcdata, dst_lsz, dstdata);
      srcdata += src_lsz;
    }
  }
  frame_buf += Npx;
}
