/**
 * @file
 * API example for decoding and filtering
 * @example filtering_video.c
 */

#include "../Common/mexClassHandler.h"
#include "../Common/ffmpegPtrs.h"
#include "../Common/ffmpegAvRedefine.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
}

#include <string>
#include <algorithm>

struct VideoReader
{
  int video_stream_index;
  std::string filter_descr;

  AVFormatContext *fmt_ctx;
  AVCodecContext *dec_ctx;
  AVFilterContext *buffersink_ctx;
  AVFilterContext *buffersrc_ctx;
  AVFilterGraph *filter_graph;

  VideoReader() : filter_descr("scale=78:24,transpose=cclock"), fmt_ctx(NULL), dec_ctx(NULL),
                  filter_graph(avfilter_graph_alloc()), video_stream_index(-1) {}
  ~VideoReader()
  {
    if (dec_ctx)
      avcodec_free_context(&dec_ctx);
    if (fmt_ctx)
      avformat_close_input(&fmt_ctx);
    if (filter_graph)
      avfilter_graph_free(&filter_graph);
  }

  void open_input_file(const std::string &filename)
  {
    int ret;
    AVCodec *dec;

    if ((ret = avformat_open_input(&fmt_ctx, filename.c_str(), NULL, NULL)) < 0)
      mexErrMsgIdAndTxt("filtering_video:open_input_file:error", "Cannot open input file");

    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
      mexErrMsgIdAndTxt("filtering_video:open_input_file:error", "Cannot find stream information");

    /* select the video stream */
    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (ret < 0)
      mexErrMsgIdAndTxt("filtering_video:open_input_file:error", "Cannot find a video stream in the input file");
    video_stream_index = ret;

    // set to ignore all other streams
    for (int i = 0; i < (int)fmt_ctx->nb_streams; i++) // for each stream
      if (i != video_stream_index)
        fmt_ctx->streams[i]->discard = AVDISCARD_ALL;

    /* create decoding context */
    dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx)
      mexErrMsgIdAndTxt("filtering_video:open_input_file:error", "Failed to allocate a decoder context");
    avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[video_stream_index]->codecpar);
    av_opt_set_int(dec_ctx, "refcounted_frames", 1, 0);

    AVDictionary *decoder_opts = NULL;

    av_dict_set(&decoder_opts, "sub_text_format", "ass", AV_DICT_DONT_OVERWRITE);

    if (!av_dict_get(decoder_opts, "threads", NULL, 0))
      av_dict_set(&decoder_opts, "threads", "auto", 0);

    /* init the video decoder */
    if ((ret = avcodec_open2(dec_ctx, dec, &decoder_opts)) < 0)
      mexErrMsgIdAndTxt("filtering_video:open_input_file:error", "Cannot open video decoder");
  }

  void init_filters()
  {
    char args[512];
    int ret = 0;
    AVFilter *buffersrc = avfilter_get_by_name("buffer");
    AVFilter *buffersink = avfilter_get_by_name("buffersink");
    ffmpeg::AVFilterInOutPtr outputs(avfilter_inout_alloc(), ffmpeg::delete_filter_inout);
    ffmpeg::AVFilterInOutPtr inputs(avfilter_inout_alloc(), ffmpeg::delete_filter_inout);
    AVRational time_base = fmt_ctx->streams[video_stream_index]->time_base;
    enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE};

    if (!outputs || !inputs || !filter_graph)
      mexErrMsgIdAndTxt("filtering_video:init_filters:error", "Failed to allocate the filter context or its AVFilterInOut's");

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
             time_base.num, time_base.den,
             dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);
mexPrintf("args=%s\n",args);
    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
    if (ret < 0)
      mexErrMsgIdAndTxt("filtering_video:init_filters:error", "Cannot create buffer source: %s", av_err2str(ret));

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
    if (ret < 0)
      mexErrMsgIdAndTxt("filtering_video:init_filters:error", "Cannot create buffer sink: %s", av_err2str(ret));

    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
      mexErrMsgIdAndTxt("filtering_video:init_filters:error", "Cannot set output pixel format: %s", av_err2str(ret));

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

    mexPrintf("filter string: %s\n", (filter_descr + tr_filter_descr).c_str());

    AVFilterInOut *in = inputs.release();
    AVFilterInOut *out = outputs.release();
    if ((ret = avfilter_graph_parse_ptr(filter_graph, (filter_descr + tr_filter_descr).c_str(),
                                        &in, &out, NULL)) < 0)
      mexErrMsgIdAndTxt("filtering_video:init_filters:error", "%s", av_err2str(ret));
    inputs.reset(in);
    outputs.reset(out);

    if (ret = avfilter_graph_config(filter_graph, NULL))
      mexErrMsgIdAndTxt("filtering_video:init_filters:error", "%s", av_err2str(ret));
  }
};

void copy_frame(const AVFrame *frame, AVRational time_base, int &cnt, uint8_t *&frame_data, double *&time)
{
  // increment the frame count
  cnt++;

  // copy time
  if (time)
  {
    if (frame->pts != AV_NOPTS_VALUE)
      *time = double(frame->pts / 100) / (AV_TIME_BASE / 100);
    time++;
  }

  int Npx = frame->height * frame->width;

  // Copy frame data
  if (frame->width == frame->linesize[0])
  {
    std::copy_n(frame->data[0], Npx, frame_data);
  }
  else
  {
    uint8_t *srcdata = frame->data[0];
    uint8_t *dstdata = frame_data;
    int src_lsz = frame->linesize[0];
    int dst_lsz = frame->width;
    if (src_lsz<0)
      srcdata -= src_lsz * (frame->height - 1);
    for (int h = 0; h < frame->height; h++)
    {
      dstdata = std::copy_n(srcdata, dst_lsz, dstdata);
      srcdata += src_lsz;
    }
  }
  frame_data += Npx;
}

// [frames,t] = filtering_video(filename,N=1,filtergraph="")
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  VideoReader reader;
  int ret;
  int Nframes = 1, cnt = 0;

  if (nrhs < 1 || !mxIsChar(prhs[0]))
    mexErrMsgIdAndTxt("filtering_video:invalidInput", "Expects a video file name as an input");
  if (nrhs > 1 && !(mxIsNumeric(prhs[1]) && mxIsScalar(prhs[1]) && mxGetScalar(prhs[1]) == (double)(Nframes = (int)mxGetScalar(prhs[1]))))
  {
    mexErrMsgIdAndTxt("filtering_video:invalidInput", "Number of requested frame must be an integer.");
  }
  if (nrhs > 2)
  {
    if (!mxIsChar(prhs[2]))
      mexErrMsgIdAndTxt("filtering_video:invalidInput", "Custom filter must be a string.");
    reader.filter_descr = mexGetString(prhs[2]);
  }

  AVPacket packet;
  ffmpeg::AVFramePtr frame(av_frame_alloc(), ffmpeg::delete_av_frame);
  ffmpeg::AVFramePtr filt_frame(av_frame_alloc(), ffmpeg::delete_av_frame);

  if (!frame || !filt_frame)
    mexErrMsgIdAndTxt("filtering_video:insufficientMemory", "Could not allocate frame");
  av_register_all();
  avfilter_register_all();

  std::string filename = mexGetString(prhs[0]);
  reader.open_input_file(filename);

  reader.init_filters();

  // initialize the output
  uint8_t *frame_data = NULL;
  double *time = NULL;
  if (nlhs > 1)
  {
    plhs[1] = mxCreateDoubleMatrix(Nframes, 1, mxREAL);
    time = mxGetPr(plhs[1]);
  }

  /* read all packets */
  while (cnt < Nframes)
  {
    if ((ret = av_read_frame(reader.fmt_ctx, &packet)) < 0)
      break;

    if (packet.stream_index == reader.video_stream_index)
    {
      ret = avcodec_send_packet(reader.dec_ctx, &packet);
      if (ret < 0)
        mexErrMsgIdAndTxt("filtering_video:error", "Error while sending a packet to the decoder: %s", av_err2str(ret));

      while (ret >= 0 && cnt < Nframes)
      {
        ret = avcodec_receive_frame(reader.dec_ctx, frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
          break;
        else if (ret < 0)
          mexErrMsgIdAndTxt("filtering_video:error", "Error while receiving a frame from the decoder: %s", av_err2str(ret));

        frame->pts = av_frame_get_best_effort_timestamp(frame.get());

        /* push the decoded frame into the filtergraph */
        if (av_buffersrc_add_frame_flags(reader.buffersrc_ctx, frame.get(), AV_BUFFERSRC_FLAG_KEEP_REF) < 0)
          mexErrMsgIdAndTxt("filtering_video:error", "Error while feeding the filtergraph: %s", av_err2str(ret));

        /* pull filtered frames from the filtergraph */
        while (cnt < Nframes)
        {
          ret = av_buffersink_get_frame(reader.buffersink_ctx, filt_frame.get());
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
          if (ret < 0)
            mexErrMsgIdAndTxt("filtering_video:error", "Error occurred: %s", av_err2str(ret));

          if (!frame_data)
          {
            mwSize dims[3] = {(mwSize)filt_frame->width, (mwSize)filt_frame->height, (mwSize)Nframes};
            plhs[0] = mxCreateNumericArray(3, dims, mxUINT8_CLASS, mxREAL);
            if (!plhs[0])
              mexErrMsgIdAndTxt("filtering_video:insufficientMemory", "Could not allocate output variables");
            frame_data = (uint8_t *)mxGetData(plhs[0]);
          }

          copy_frame(filt_frame.get(), reader.buffersink_ctx->inputs[0]->time_base, cnt, frame_data, time);
          av_frame_unref(filt_frame.get());
        }
        av_frame_unref(frame.get());
      }
    }
    av_packet_unref(&packet);
  }
}
