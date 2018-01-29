#include "ffmpegFilterGraph.h"

// #include <stdint.h>

extern "C" {
// #include "libavfilter/avfilter.h"
// #include "libavfilter/buffersink.h"
// #include "libavfilter/buffersrc.h"

// #include "libavresample/avresample.h"

// #include "libavutil/avassert.h"
// #include "libavutil/avstring.h"
// #include "libavutil/bprint.h"
// #include "libavutil/channel_layout.h"
// #include "libavutil/display.h"
// #include "libavutil/opt.h"
// #include "libavutil/pixdesc.h"
// #include "libavutil/pixfmt.h"
// #include "libavutil/imgutils.h"
// #include "libavutil/samplefmt.h"
}

using namespace ffmpeg;

std::string OutputVideoFilter::choose_pix_fmts()
// static char *choose_pix_fmts(OutputFilter *ofilter)
{
  if (!ost)
    return "";

  std::string rval;

  AVPixelFormats fmts = dynamic_cast<OutputVideoStream *>(ost)->choose_pix_fmts();

  if (size(fmts) == 1 && fmts[0] == AV_PIX_FMT_NONE) // use as propagated
  {
    avfilter_graph_set_auto_convert(graph->graph, AVFILTER_AUTO_CONVERT_NONE);
    rval = av_get_pix_fmt_name(ost->getPixelFormat());
  }
  else
  {
    auto p = fmts.begin();
    rval = av_get_pix_fmt_name(*p);
    for (++p; p < fmts.end() && *p != AV_PIX_FMT_NONE; ++p)
    {
      rval += "|";
      rval += av_get_pix_fmt_name(*p);
    }
  }

  return rval;
}

/* Define a function for building a string containing a list of
 * allowed formats. */
#define DEF_CHOOSE_FORMAT(suffix, type, var, supported_list, none, get_name) \
  \
std::string OutputAudioFilter::choose_##suffix()                             \
  \
{                                                                         \
    if (var != none)                                                         \
    {                                                                        \
      get_name(var);                                                         \
      return name;                                                           \
    }                                                                        \
    else if (supported_list)                                                 \
    {                                                                        \
      const type *p;                                                         \
      std::string ret;                                                       \
                                                                             \
      p = supported_list;                                                    \
      if (*p != none)                                                        \
      {                                                                      \
        ret = get_name(*p);                                                  \
        for (++p; *p != none; ++p)                                           \
        {                                                                    \
          ret += '|';                                                        \
          ret += get_name(*p);                                               \
        }                                                                    \
      }                                                                      \
      return ret;                                                            \
    }                                                                        \
    else                                                                     \
      return "";                                                             \
  \
}

//DEF_CHOOSE_FORMAT(pix_fmts, enum AVPixelFormat, format, formats, AV_PIX_FMT_NONE,
//                  GET_PIX_FMT_NAME)

DEF_CHOOSE_FORMAT(sample_fmts, AVSampleFormat, format, formats,
                  AV_SAMPLE_FMT_NONE, GET_SAMPLE_FMT_NAME)

DEF_CHOOSE_FORMAT(sample_rates, int, sample_rate, sample_rates, 0,
                  GET_SAMPLE_RATE_NAME)

DEF_CHOOSE_FORMAT(channel_layouts, uint64_t, channel_layout, channel_layouts, 0,
                  GET_CH_LAYOUT_NAME)

int FilterGraph::init_simple_filtergraph(InputStream &ist, OutputStream &ost)
{
  switch (ost.getAVMediaType())
  {
  case AVMEDIA_TYPE_VIDEO:
    outputs.push_back(new OutputVideoFilter(graph, ost));
    break;
  case AVMEDIA_TYPE_AUDIO:
    outputs.push_back(new OutputAudioFilter(graph, ost));
    break;
  default:
    throw ffmpegException("Only video and audio filters supported currently.");
  }

  switch (ist.getAVMediaType())
  {
  case AVMEDIA_TYPE_VIDEO:
    inputs.push_back(new InputVideoFilter(graph, ist));
    break;
  case AVMEDIA_TYPE_AUDIO:
    inputs.push_back(new InputAudioFilter(graph, ist));
    break;
  default:
    throw ffmpegException("Only video and audio filters supported currently.");
  }

  return 0;
}

std::string FilterGraph::describe_filter_link(AVFilterInOut *inout, int in)
{
  AVFilterContext *ctx = inout->filter_ctx;
  AVFilterPad *pads = in ? ctx->input_pads : ctx->output_pads;
  int nb_pads = in ? ctx->nb_inputs : ctx->nb_outputs;

  std::string res = ctx->filter->name;
  if (nb_pads > 1)
  {
    res += ':';
    res += avfilter_pad_get_name(pads, inout->pad_idx);
  }
  return res;
}

void FilterGraph::init_input_filter(AVFilterInOut *in, InputStream &ist)
{
  AVMediaType type = avfilter_pad_get_type(in->filter_ctx->input_pads, in->pad_idx);
  int i;

  // TODO: support other filter types
  if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO)
    throw ffmpegException("Only video and audio filters supported currently.");
  if (type != ist.getAVMediaType())
    throw ffmpegException("Media type mismatch between AVFilterInOut and InputStream.");

  switch (ist.getAVMediaType())
  {
  case AVMEDIA_TYPE_VIDEO:
    inputs.push_back(new InputVideoFilter(graph, ist));
    break;
  case AVMEDIA_TYPE_AUDIO:
    inputs.push_back(new InputAudioFilter(graph, ist));
    break;
  default:
    throw ffmpegException("Only video and audio filters supported currently.");
  }
  // fg->inputs[fg->nb_inputs - 1]->ist = ist;
  // fg->inputs[fg->nb_inputs - 1]->graph = fg;
  // fg->inputs[fg->nb_inputs - 1]->format = -1;
}

void FilterGraph::init_complex_filtergraph(const std::string &new_desc = "")
{
  AVFilterInOut *in, *out, *cur;
  AVFilterGraph *test_graph;
  int ret = 0;

  /* this graph is only used for determining the kinds of inputs
     * and outputs we have, and is discarded on exit from this function */
  test_graph = avfilter_graph_alloc();
  if (!test_graph)
    throw ffmpegException(AVERROR(ENOMEM));

  ret = avfilter_graph_parse2(test_graph, (new_desc.size()) ? new_desc.c_str() : graph_desc.c_str(), &in, &out);
  if (ret < 0)
    throw ffmpegException("Failed to parse filter graph description.");

  try
  {
    // create input endpoints
    for (cur = in; cur; cur = cur->next)
      init_input_filter(cur);

    // create output endpoints
    for (cur = out; cur;)
    {
      switch (avfilter_pad_get_type(cur->filter_ctx->output_pads, cur->pad_idx))
      {
      case AVMEDIA_TYPE_VIDEO:
      outputs.push_back(new OutputVideoFilter;
      break;
    case AVMEDIA_TYPE_AUDIO:
      outputs.push_back(new OutputAudioFilter);
      break;
    default:
      throw ffmpegException("Only video and audio filters supported currently.");
      }
      // OutputFilter *of = outputs.back();
      // of->graph = fg;
      // of->out_tmp = cur;
      // of->type = ;
      // of->name = describe_filter_link(cur, 0);
      // cur = cur->next;
      // of->out_tmp->next = NULL;
    }
  }
  catch
  {
    avfilter_inout_free(&in);
    avfilter_graph_free(&graph);
  }
  avfilter_inout_free(&in);
  avfilter_graph_free(&graph);
}

// used to append filters for autorotate feature
int FilterGraph::insert_filter(AVFilterContext *&last_filter, int &pad_idx,
                               const std::string &filter_name, const std::string &args)
{
  AVFilterGraph *graph = last_filter->graph;
  AVFilterContext *ctx;
  int ret;

  ret = avfilter_graph_create_filter(&ctx,
                                     avfilter_get_by_name(filter_name.c_str()),
                                     filter_name.c_str(), args.c_str(), NULL, graph);
  if (ret < 0)
    return ret;

  ret = avfilter_link(last_filter, pad_idx, ctx, 0);
  if (ret < 0)
    return ret;

  last_filter = ctx;
  pad_idx = 0;
  return 0;
}

OutputFilter* configure_output_filter(AVFilterInOut *out)
{
  switch (avfilter_pad_get_type(out->filter_ctx->output_pads, out->pad_idx))
  {
  case AVMEDIA_TYPE_VIDEO:
    return new OutputVideoFilter();
  case AVMEDIA_TYPE_AUDIO:
    return new OutputAudioFilter();
  default:
    av_assert0(0);
  }
}

void check_filter_outputs()
{
  for (auto output = outputs.begin(); output < outputs.end(); ++output)
  {
    if (!output->sink)
      throw ffmpegException("Filter %s has an unconnected output\n", output->name);
  }
}

InputFilter *FilterGraph::configure_input_filter(AVFilterInOut *in)
{
  switch (avfilter_pad_get_type(in->filter_ctx->input_pads, in->pad_idx))
  {
  case AVMEDIA_TYPE_VIDEO:
    return InputVideoFilter(fg);
  case AVMEDIA_TYPE_AUDIO:
    return InputAudioFilter(fg);
  default:
    av_assert0(0);
  }
}

void FilterGraph::cleanup()
{
  for (auto it = outputs.begin(); it<outputs.end(); ++it) delete *it;
  outputs.clear();
  for (auto it = inputs.begin(); it<inputs.end(); ++it) delete *it;
  inputs.clear();
  avfilter_graph_free(&graph);
}

void FilterGraph::configure()
{
  AVFilterInOut *inputs, *outputs, *cur;
  int ret, i, simple = filtergraph_is_simple(fg);
  const char *graph_desc = simple ? fg->outputs[0]->ost->avfilter : fg->graph_desc;

  cleanup_filtergraph(fg);
  if (!(fg->graph = avfilter_graph_alloc()))
    return AVERROR(ENOMEM);

  if (simple)
  {
    OutputStream *ost = fg->outputs[0]->ost;
    char args[512];
    AVDictionaryEntry *e = NULL;

    fg->graph->nb_threads = filter_nbthreads;

    args[0] = 0;
    while ((e = av_dict_get(ost->sws_dict, "", e,
                            AV_DICT_IGNORE_SUFFIX)))
    {
      av_strlcatf(args, sizeof(args), "%s=%s:", e->key, e->value);
    }
    if (strlen(args))
      args[strlen(args) - 1] = 0;
    fg->graph->scale_sws_opts = av_strdup(args);

    args[0] = 0;
    while ((e = av_dict_get(ost->swr_opts, "", e,
                            AV_DICT_IGNORE_SUFFIX)))
    {
      av_strlcatf(args, sizeof(args), "%s=%s:", e->key, e->value);
    }
    if (strlen(args))
      args[strlen(args) - 1] = 0;
    av_opt_set(fg->graph, "aresample_swr_opts", args, 0);

    args[0] = '\0';
    while ((e = av_dict_get(fg->outputs[0]->ost->resample_opts, "", e,
                            AV_DICT_IGNORE_SUFFIX)))
    {
      av_strlcatf(args, sizeof(args), "%s=%s:", e->key, e->value);
    }
    if (strlen(args))
      args[strlen(args) - 1] = '\0';

    e = av_dict_get(ost->encoder_opts, "threads", NULL, 0);
    if (e)
      av_opt_set(fg->graph, "threads", e->value, 0);
  }
  else
  {
    fg->graph->nb_threads = filter_complex_nbthreads;
  }

  if ((ret = avfilter_graph_parse2(fg->graph, graph_desc, &inputs, &outputs)) < 0)
    goto fail;

  if (filter_hw_device || hw_device_ctx)
  {
    AVBufferRef *device = filter_hw_device ? filter_hw_device->device_ref
                                           : hw_device_ctx;
    for (i = 0; i < fg->graph->nb_filters; i++)
    {
      fg->graph->filters[i]->hw_device_ctx = av_buffer_ref(device);
      if (!fg->graph->filters[i]->hw_device_ctx)
      {
        ret = AVERROR(ENOMEM);
        goto fail;
      }
    }
  }

  if (simple && (!inputs || inputs->next || !outputs || outputs->next))
  {
    const char *num_inputs;
    const char *num_outputs;
    if (!outputs)
    {
      num_outputs = "0";
    }
    else if (outputs->next)
    {
      num_outputs = ">1";
    }
    else
    {
      num_outputs = "1";
    }
    if (!inputs)
    {
      num_inputs = "0";
    }
    else if (inputs->next)
    {
      num_inputs = ">1";
    }
    else
    {
      num_inputs = "1";
    }
    av_log(NULL, AV_LOG_ERROR, "Simple filtergraph '%s' was expected "
                               "to have exactly 1 input and 1 output."
                               " However, it had %s input(s) and %s output(s)."
                               " Please adjust, or use a complex filtergraph (-filter_complex) instead.\n",
           graph_desc, num_inputs, num_outputs);
    ret = AVERROR(EINVAL);
    goto fail;
  }

  for (cur = inputs, i = 0; cur; cur = cur->next, i++)
    if ((ret = configure_input_filter(fg, fg->inputs[i], cur)) < 0)
    {
      avfilter_inout_free(&inputs);
      avfilter_inout_free(&outputs);
      goto fail;
    }
  avfilter_inout_free(&inputs);

  for (cur = outputs, i = 0; cur; cur = cur->next, i++)
    fg->outputs[i] = configure_output_filter(fg, cur);
  avfilter_inout_free(&outputs);

  if ((ret = avfilter_graph_config(fg->graph, NULL)) < 0)
    goto fail;

  /* limit the lists of allowed formats to the ones selected, to
     * make sure they stay the same if the filtergraph is reconfigured later */
  for (i = 0; i < fg->nb_outputs; i++)
  {
    OutputFilter *ofilter = fg->outputs[i];
    AVFilterContext *sink = ofilter->filter;

    ofilter->format = av_buffersink_get_format(sink);

    ofilter->width = av_buffersink_get_w(sink);
    ofilter->height = av_buffersink_get_h(sink);

    ofilter->sample_rate = av_buffersink_get_sample_rate(sink);
    ofilter->channel_layout = av_buffersink_get_channel_layout(sink);
  }

  fg->reconfiguration = 1;

  for (i = 0; i < fg->nb_outputs; i++)
  {
    OutputStream *ost = fg->outputs[i]->ost;
    if (!ost->enc)
    {
      /* identical to the same check in ffmpeg.c, needed because
               complex filter graphs are initialized earlier */
      av_log(NULL, AV_LOG_ERROR, "Encoder (codec %s) not found for output stream #%d:%d\n",
             avcodec_get_name(ost->st->codecpar->codec_id), ost->file_index, ost->index);
      ret = AVERROR(EINVAL);
      goto fail;
    }
    if (ost->enc->type == AVMEDIA_TYPE_AUDIO &&
        !(ost->enc->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
      av_buffersink_set_frame_size(ost->filter->filter,
                                   ost->enc_ctx->frame_size);
  }

  for (i = 0; i < fg->nb_inputs; i++)
  {
    while (av_fifo_size(fg->inputs[i]->frame_queue))
    {
      AVFrame *tmp;
      av_fifo_generic_read(fg->inputs[i]->frame_queue, &tmp, sizeof(tmp), NULL);
      ret = av_buffersrc_add_frame(fg->inputs[i]->filter, tmp);
      av_frame_free(&tmp);
      if (ret < 0)
        goto fail;
    }
  }

  /* send the EOFs for the finished inputs */
  for (i = 0; i < fg->nb_inputs; i++)
  {
    if (fg->inputs[i]->eof)
    {
      ret = av_buffersrc_add_frame(fg->inputs[i]->filter, NULL);
      if (ret < 0)
        goto fail;
    }
  }

  /* process queued up subtitle packets */
  for (i = 0; i < fg->nb_inputs; i++)
  {
    InputStream *ist = fg->inputs[i]->ist;
    if (ist->sub2video.sub_queue && ist->sub2video.frame)
    {
      while (av_fifo_size(ist->sub2video.sub_queue))
      {
        AVSubtitle tmp;
        av_fifo_generic_read(ist->sub2video.sub_queue, &tmp, sizeof(tmp), NULL);
        sub2video_update(ist, &tmp);
        avsubtitle_free(&tmp);
      }
    }
  }

  return 0;

fail:
  cleanup_filtergraph(fg);
  return ret;
}

int ifilter_parameters_from_frame(InputFilter *ifilter, const AVFrame *frame)
{
  av_buffer_unref(&ifilter->hw_frames_ctx);

  ifilter->format = frame->format;

  ifilter->width = frame->width;
  ifilter->height = frame->height;
  ifilter->sample_aspect_ratio = frame->sample_aspect_ratio;

  ifilter->sample_rate = frame->sample_rate;
  ifilter->channels = frame->channels;
  ifilter->channel_layout = frame->channel_layout;

  if (frame->hw_frames_ctx)
  {
    ifilter->hw_frames_ctx = av_buffer_ref(frame->hw_frames_ctx);
    if (!ifilter->hw_frames_ctx)
      return AVERROR(ENOMEM);
  }

  return 0;
}

int ist_in_filtergraph(FilterGraph *fg, InputStream *ist)
{
  int i;
  for (i = 0; i < fg->nb_inputs; i++)
    if (fg->inputs[i]->ist == ist)
      return 1;
  return 0;
}

int filtergraph_is_simple(FilterGraph *fg)
{
  return !fg->graph_desc;
}
