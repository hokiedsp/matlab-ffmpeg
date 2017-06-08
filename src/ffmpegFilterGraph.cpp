#include "ffmpegFilterGraph.h"

#include <sstream>   // ostringstream
#include <algorithm> // fill_n
#include <cmath>     // fabs
#include <cstring>
#include <memory>
#include <functional>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/avassert.h> // for AVDictionary
#include <libavutil/bprint.h>   // for AVDictionary
}

#include "ffmpegException.h"
#include "ffmpegInputFile.h"
#include "ffmpegInputStream.h"
#include "ffmpegOutputFile.h"
#include "ffmpegOutputStream.h"

using namespace ffmpeg;

typedef std::unique_ptr<AVFilterInOut, std::function<void(AVFilterInOut *)>> AvFilterInOutPtr;
void delete_avfilter_inout(AVFilterInOut *inout) { avfilter_inout_free(&inout); }

typedef std::unique_ptr<AVFilterGraph, std::function<void(AVFilterGraph *)>> AvFilterGraphPtr;
void delete_avfilter_graph(AVFilterGraph *ctx) { avfilter_graph_free(&ctx); }

typedef std::unique_ptr<AVBufferSrcParameters, std::function<void(AVBufferSrcParameters *)>> AvBufferSrcParametersPtr;
void delete_avbuffersrcparameters(AVBufferSrcParameters *par) { av_freep(&par); }

void Filter::describe_filter_link(AVFilterInOut *inout, bool in)
{
   AVFilterContext *ctx = inout->filter_ctx;
   AVFilterPad *pads = in ? ctx->input_pads : ctx->output_pads;
   int nb_pads = in ? ctx->nb_inputs : ctx->nb_outputs;
   
   std::ostringstream pb;
   pb << ctx->filter->name;
   if (nb_pads > 1)
      pb << ":" << avfilter_pad_get_name(pads, inout->pad_idx);

   name = pb.str();
}

InputFilter::InputFilter(FilterGraph &f, InputStream &s) : Filter(f), ist(s)
{
   // update the decoding
   ist.input_to_filter(*this);
}

// complex filtergraph
FilterGraph::FilterGraph(const int i, const std::string &desc) : index(i), graph_desc(desc), graph(NULL), reconfiguration(0)
{
}

// simple filtergraph
FilterGraph::FilterGraph(const int i, InputStream &ist, OutputStream &ost) : index(i), graph(NULL), reconfiguration(0)
{
   // outputs.emplace_back(*this, ost);
   // ost.filter = &fg.front();

   // inputs.emplace_back(*this, ist);
   // ist.filters.push_back(inputs.front());
}

FilterGraph::~FilterGraph()
{
}

std::string FilterGraph::options_to_string(const AVDictionary *dict)
{
   AVDictionaryEntry *e=NULL;
   std::ostringstream args; // -complex_filter option argument
   while ((e = av_dict_get(dict, "", e, AV_DICT_IGNORE_SUFFIX)))
   {
      if (args.str().size())
         args << ":";
      args << e->key << "=" << e->value;
   }
   return args.str();
}

int FilterGraph::configure_filtergraph()
{
   int ret;

   avfilter_graph_free(&graph);
   if (!(graph = avfilter_graph_alloc()))
      return AVERROR(ENOMEM);

   bool simple = filtergraph_is_simple(); // true if filter graph is simple
   if (simple)                            // prepare additional inline filters specified in options
   {
      OutputStream &ost = *outputs[0].ost;
      std::string args; // -complex_filter option argument

      // video scale option (libswscale): parse the scale filter options
      args = options_to_string(ost.sws_dict);
      graph->scale_sws_opts = av_strdup(args.c_str()); // set scale filter options

      // audio resampling option (libswresample)
      args = options_to_string(ost.swr_opts);
      av_opt_set(graph, "aresample_swr_opts", args.c_str(), 0);

      // the other audio resampling option (libavresample, not bundled with Windows ffMpeg build)
      args = options_to_string(outputs[0].ost->resample_opts);
      graph->resample_lavr_opts = av_strdup(args.c_str());

      // if multithreading is enabled
      AVDictionaryEntry *e = av_dict_get(ost.encoder_opts, "threads", NULL, 0);
      if (e)
         av_opt_set(graph, "threads", e->value, 0);
   }

   //
   AVFilterInOut *in, *out;
   const std::string desc = simple ? outputs[0].ost->avfilter : graph_desc;
   if ((ret = avfilter_graph_parse2(graph, desc.c_str(), &in, &out)) < 0) // create unlinked filters
      return ret;

   // if (hw_device_ctx)
   // {
   //    for (i = 0; i < graph->nb_filters; i++)
   //    {
   //       graph->filters[i]->hw_device_ctx = av_buffer_ref(hw_device_ctx);
   //    }
   // }

   // if both simple & complex filtergraphs are
   if (simple && (!in || in->next || !out || out->next))
   {
      const char *num_inputs;
      const char *num_outputs;
      if (!out)
         num_outputs = "0";
      else if (out->next)
         num_outputs = ">1";
      else
         num_outputs = "1";

      if (!in)
         num_inputs = "0";
      else if (in->next)
         num_inputs = ">1";
      else
         num_inputs = "1";

      av_log(NULL, AV_LOG_ERROR, "Simple filtergraph '%s' was expected "
                                 "to have exactly 1 input and 1 output."
                                 " However, it had %s input(s) and %s output(s)."
                                 " Please adjust, or use a complex filtergraph (-filter_complex) instead.\n",
             graph_desc, num_inputs, num_outputs);
      return AVERROR(EINVAL);
   }

   // traverse through filtergraph
   int i = 0;
   for (AVFilterInOut *cur = in; cur; cur = cur->next)
   {
      if ((ret = configure_input_filter(inputs[i++], cur)) < 0)
      {
         avfilter_inout_free(&in);
         avfilter_inout_free(&out);
         return ret;
      }
   }
   avfilter_inout_free(&in);

   i = 0;
   for (AVFilterInOut *cur = out; cur; cur = cur->next)
      configure_output_filter(outputs[i++], cur);
   avfilter_inout_free(&out);

   if ((ret = avfilter_graph_config(graph, NULL)) < 0)
      return ret;

   reconfiguration = 1;

   for (OutputFilters::iterator it = outputs.begin(); it < outputs.end(); it++)
   {
      OutputStream &ost = *it->ost;
      if (!ost.enc)
      {
         /* identical to the same check in ffmpeg.c, needed because
               complex filter graphs are initialized earlier */
         av_log(NULL, AV_LOG_ERROR, "Encoder (codec %s) not found for output stream #%d:%d\n",
                avcodec_get_name(ost.st->codecpar->codec_id), ost.file.index, ost.index);
         return AVERROR(EINVAL);
      }
      if (ost.enc->type == AVMEDIA_TYPE_AUDIO &&
          !(ost.enc->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
         av_buffersink_set_frame_size(ost.filter->filter,
                                      ost.enc_ctx->frame_size);
   }

   return 0;
}

int FilterGraph::configure_input_video_filter(InputFilter &ifilter, AVFilterInOut *in)
{
   int ret;
   InputStream &ist = (InputStream &)ifilter.ist;

   if (ist.get_codec_type() == AVMEDIA_TYPE_AUDIO)
   {
      av_log(NULL, AV_LOG_ERROR, "Cannot connect video filter to audio input\n");
      ret = AVERROR(EINVAL);
      return ret;
   }

   AVRational fr = ist.get_framerate();

   // prepare subtitle input stream
   if (ist.get_codec_type() == AVMEDIA_TYPE_SUBTITLE)
   {
      ret = ((SubtitleInputStream&)ist).sub2video_prepare();
      if (ret < 0)
         return ret;
   }

   std::ostringstream name;
   name << "graph " << index << " input from stream " << ist.file.index << ":" << ist.st->index;
   if ((ret = avfilter_graph_create_filter(&ifilter.filter, avfilter_get_by_name("buffer"), name.str().c_str(), ist.get_buffer_filt_args().c_str(), NULL, graph)) < 0)
      return ret;

   AvBufferSrcParametersPtr par(av_buffersrc_parameters_alloc(), delete_avbuffersrcparameters);
   std::fill_n((uint8_t *)(par.get()), sizeof(AVBufferSrcParameters), 0);
   par->format = AV_PIX_FMT_NONE;
   if (ist.get_codec_type() == AVMEDIA_TYPE_VIDEO)
      par->hw_frames_ctx = ((VideoInputStream&)ist).get_hw_frames_ctx();
   if ((ret = av_buffersrc_parameters_set(ifilter.filter, par.get())) < 0)
      return ret;

   InputFile &f = ist.file;
   int pad_idx = 0;
   
   AVFilterContext *last_filter = ifilter.filter;

   if (ist.auto_rotate())
   {
      // insert a series of filter to perform the necessary rotation
      double theta = ist.get_rotation();

      if (std::fabs(theta - 90.0) < 1.0)
      {
         ret = insert_filter(last_filter, pad_idx, "transpose", "clock");
      }
      else if (fabs(theta - 180.0) < 1.0)
      {
         ret = insert_filter(last_filter, pad_idx, "hflip", "");
         if (ret < 0)
            return ret;
         ret = insert_filter(last_filter, pad_idx, "vflip", "");
      }
      else if (fabs(theta - 270.0) < 1.0)
      {
         ret = insert_filter(last_filter, pad_idx, "transpose", "cclock");
      }
      else if (fabs(theta) > 1.0)
      {
         std::ostringstream rotate_buf;
         rotate_buf << theta << "*PI/180";
         ret = insert_filter(last_filter, pad_idx, "rotate", rotate_buf.str());
      }
      if (ret < 0)
         return ret;
   }

   if (ist.get_framerate().num)
   {
      AVFilterContext *setpts;

      name.str(""); // clear the string
      name << "force CFR for input from stream " << ist.file.index << ":" << ist.st->index;
      if ((ret = avfilter_graph_create_filter(&setpts, avfilter_get_by_name("setpts"), name.str().c_str(), "N", NULL, graph)) < 0)
         return ret;

      if ((ret = avfilter_link(last_filter, 0, setpts, 0)) < 0)
         return ret;

      last_filter = setpts;
   }

   if (do_deinterlace)
   {
      AVFilterContext *yadif;

      name.str("");
      name << "deinterlace input from stream " << ist.file.index << ":" << ist.st->index;

      if ((ret = avfilter_graph_create_filter(&yadif, avfilter_get_by_name("yadif"), name.str().c_str(), "", NULL, graph)) < 0)
         return ret;

      if ((ret = avfilter_link(last_filter, 0, yadif, 0)) < 0)
         return ret;

      last_filter = yadif;
   }

   name.str("");
   name << "trim for input stream " << ist.file.index << ":" << ist.st->index;

   ret = insert_trim((copy_ts) ? f.get_tsoffset(start_at_zero) : 0, f.recording_time, last_filter, pad_idx, name.str());
   if (ret < 0)
      return ret;

   if ((ret = avfilter_link(last_filter, 0, in->filter_ctx, in->pad_idx)) < 0)
      return ret;
   return 0;
}

int FilterGraph::auto_insert_filter_input(InputStream &ist, const std::string &opt_name, const std::string & filter_name, const std::string &arg, AVFilterContext *&last_filter) const
{
   AVFilterContext *filt_ctx;

   // av_log(NULL, AV_LOG_INFO, opt_name " is forwarded to lavfi similarly to -af " filter_name "=%s.\n", arg);

   std::ostringstream name;
   name << "graph " << index << " " << filter_name << " for input stream " << ist.file.index << ":" << ist.st->index;
    
   int ret = avfilter_graph_create_filter(&filt_ctx, avfilter_get_by_name(filter_name.c_str()), name.str().c_str(), arg.c_str(), NULL, graph);
   if (ret < 0)
      return ret;

   ret = avfilter_link(last_filter, 0, filt_ctx, 0);
   if (ret < 0)
      return ret;
   last_filter = filt_ctx;
   return ret;
}

int FilterGraph::auto_insert_filter(const std::string &opt_name, const std::string &filter_name, const std::string &arg, int &pad_idx, AVFilterContext *&last_filter) const
{
   AVFilterContext *filt_ctx;

   // av_log(NULL, AV_LOG_INFO, opt_name " is forwarded to lavfi similarly to -af " filter_name "=%s.\n", arg);

   int ret = avfilter_graph_create_filter(&filt_ctx, avfilter_get_by_name(filter_name.c_str()), filter_name.c_str(), arg.c_str(), NULL, graph);
   if (ret < 0)
      return ret;

   ret = avfilter_link(last_filter, pad_idx, filt_ctx, 0);
   if (ret < 0)
      return ret;

   last_filter = filt_ctx;
   pad_idx = 0;
   return ret;
}

int FilterGraph::configure_input_audio_filter(InputFilter &ifilter, AVFilterInOut *in)
{
   int ret;
   AudioInputStream &ist = (AudioInputStream &)ifilter.ist;
   if (ist.dec_ctx->codec_type != AVMEDIA_TYPE_AUDIO)
   {
      av_log(NULL, AV_LOG_ERROR, "Cannot connect audio filter to non audio input\n");
      return AVERROR(EINVAL);
   }

   {
      std::ostringstream args;
      args << "time_base=" << 1 << "/" << ist.dec_ctx->sample_rate << ":sample_rate=" << ist.dec_ctx->sample_rate << ":"
           << "sample_fmt=" << std::string(av_get_sample_fmt_name(ist.dec_ctx->sample_fmt));
      if (ist.dec_ctx->channel_layout)
         args << ":channel_layout=0x" << ist.dec_ctx->channel_layout;
      else
         args << ":channels=" << ist.dec_ctx->channels;

      std::ostringstream name;
      name << "graph " << index << " input from stream " << ist.file.index << ":" << ist.st->index;

      const AVFilter *abuffer_filt = avfilter_get_by_name("abuffer");
      if ((ret = avfilter_graph_create_filter(&ifilter.filter, abuffer_filt, name.str().c_str(), args.str().c_str(), NULL, graph)) < 0)
         return ret;
   }

   AVFilterContext *last_filter = ifilter.filter;

   if (audio_sync_method > 0)
   {
      std::ostringstream args;
      args << "async=" << audio_sync_method;
      if (audio_drift_threshold != 0.1)
         args << ":min_hard_comp=" << audio_drift_threshold;
      if (!reconfiguration)
         args << ":first_pts=0";

      auto_insert_filter_input(ist, "-async", "aresample", args.str(), last_filter);
   }

   InputFile &f = ist.file;
   int pad_idx = 0;
   std::ostringstream name;
   name << "trim for input stream " << ist.file.index << ":" << ist.st->index;
   ret = insert_trim((copy_ts) ? f.get_tsoffset(start_at_zero) : 0, f.recording_time, last_filter, pad_idx, name.str());
   if (ret < 0)
      return ret;

   if ((ret = avfilter_link(last_filter, 0, in->filter_ctx, in->pad_idx)) < 0)
      return ret;

   return 0;
}

int FilterGraph::configure_input_filter(InputFilter &ifilter, AVFilterInOut *in)
{
   ifilter.describe_filter_link(in);

   if (!ifilter.ist.dec)
   {
      // av_log(NULL, AV_LOG_ERROR, "No decoder for stream #%d:%d, filtering impossible\n", ifilter.ist.file.index, ifilter.ist.st->index);
      return AVERROR_DECODER_NOT_FOUND;
   }
   switch (avfilter_pad_get_type(in->filter_ctx->input_pads, in->pad_idx))
   {
   case AVMEDIA_TYPE_VIDEO:
      return configure_input_video_filter(ifilter, in);
   case AVMEDIA_TYPE_AUDIO:
      return configure_input_audio_filter(ifilter, in);
   default:
      av_assert0(0);
   }
}

bool FilterGraph::ist_in_filtergraph(InputStream &ist)
{
   for (InputFilters::iterator it = inputs.begin(); it < inputs.end(); it++)
      if (&it->ist == &ist)
         return true;
   return false;
}

bool FilterGraph::filtergraph_is_simple()
{
   return graph_desc.empty();
}

// static function
int FilterGraph::insert_trim(int64_t start_time, int64_t duration, AVFilterContext *&last_filter, int &pad_idx, const std::string &filter_name)
{
   AVFilterGraph *graph = last_filter->graph;
   AVFilterContext *ctx;
   const AVFilter *trim;
   enum AVMediaType type = avfilter_pad_get_type(last_filter->output_pads, pad_idx);
   const std::string name = (type == AVMEDIA_TYPE_VIDEO) ? "trim" : "atrim";
   int ret = 0;

   if (duration == INT64_MAX && start_time == AV_NOPTS_VALUE)
      return 0;

   trim = avfilter_get_by_name(name.c_str());
   if (!trim)
   {
      av_log(NULL, AV_LOG_ERROR, "%s filter not present, cannot limit recording time.\n", name.c_str());
      return AVERROR_FILTER_NOT_FOUND;
   }

   ctx = avfilter_graph_alloc_filter(graph, trim, filter_name.c_str());
   if (!ctx)
      return AVERROR(ENOMEM);

   if (duration != INT64_MAX)
   {
      ret = av_opt_set_int(ctx, "durationi", duration, AV_OPT_SEARCH_CHILDREN);
   }
   if (ret >= 0 && start_time != AV_NOPTS_VALUE)
   {
      ret = av_opt_set_int(ctx, "starti", start_time, AV_OPT_SEARCH_CHILDREN);
   }
   if (ret < 0)
   {
      av_log(ctx, AV_LOG_ERROR, "Error configuring the %s filter", name.c_str());
      return ret;
   }

   ret = avfilter_init_str(ctx, NULL);
   if (ret < 0)
      return ret;

   ret = avfilter_link(last_filter, pad_idx, ctx, 0);
   if (ret < 0)
      return ret;

   last_filter = ctx;
   pad_idx = 0;
   return 0;
}

int FilterGraph::insert_filter(AVFilterContext *&last_filter, int &pad_idx, const std::string &filter_name, const std::string &args)
{
   AVFilterGraph *graph = last_filter->graph;
   AVFilterContext *ctx;
   int ret = avfilter_graph_create_filter(&ctx, avfilter_get_by_name(filter_name.c_str()), filter_name.c_str(), args.c_str(), NULL, graph);
   if (ret < 0)
      return ret;

   ret = avfilter_link(last_filter, pad_idx, ctx, 0);
   if (ret < 0)
      return ret;

   last_filter = ctx;
   pad_idx = 0;
   return 0;
}

int FilterGraph::configure_output_video_filter(OutputFilter &ofilter, AVFilterInOut *out)
{
   int ret;
   AVFilterContext *last_filter = out->filter_ctx;
   int pad_idx = out->pad_idx;

   OutputStream &ost = *ofilter.ost;
   std::ostringstream name;
   name << "output stream " << ost.file.index << ":" << ost.index;
   ret = avfilter_graph_create_filter(&ofilter.filter, avfilter_get_by_name("buffersink"), name.str().c_str(), NULL, NULL, graph);
   if (ret < 0)
      return ret;

   AVCodecContext *codec = ost.enc_ctx;
   if (!hw_device_ctx && (codec->width || codec->height))
   {
      AVFilterContext *filter;
      AVDictionaryEntry *e = NULL;

      std::ostringstream args;
      args << codec->width << ":" << codec->height;
      while ((e = av_dict_get(ost.sws_dict, "", e, AV_DICT_IGNORE_SUFFIX)))
         args << ":" << e->key << ":" << e->value;

      name.str("");
      name << "scaler for output stream " << ost.file.index << ":" << ost.index;
      if ((ret = avfilter_graph_create_filter(&filter, avfilter_get_by_name("scale"), name.str().c_str(), args.str().c_str(), NULL, graph)) < 0)
         return ret;
      if ((ret = avfilter_link(last_filter, pad_idx, filter, 0)) < 0)
         return ret;

      last_filter = filter;
      pad_idx = 0;
   }

   std::string pix_fmts;
   if ((pix_fmts = ost.choose_pix_fmts()).size())
   {
      AVFilterContext *filter;

      ret = avfilter_graph_create_filter(&filter, avfilter_get_by_name("format"), "format", pix_fmts.c_str(), NULL, graph);
      if (ret < 0)
         return ret;
      if ((ret = avfilter_link(last_filter, pad_idx, filter, 0)) < 0)
         return ret;

      last_filter = filter;
      pad_idx = 0;
   }

   name.str("");
   name << "trim for output stream " << ost.file.index << ":" << ost.index;
   OutputFile &of = ost.file;
   ret = insert_trim(of.start_time, of.recording_time, last_filter, pad_idx, name.str());
   if (ret < 0)
      return ret;

   if ((ret = avfilter_link(last_filter, pad_idx, ofilter.filter, 0)) < 0)
      return ret;

   return 0;
}

int FilterGraph::configure_output_audio_filter(OutputFilter &ofilter, AVFilterInOut *out)
{
   int ret;
   std::ostringstream name;

   AVFilterContext *last_filter = out->filter_ctx;
   int pad_idx = out->pad_idx;

   OutputStream &ost = *ofilter.ost;

   name << "output stream " << ost.file.index << ":" << ost.index;
   ret = avfilter_graph_create_filter(&ofilter.filter, avfilter_get_by_name("abuffersink"), name.str().c_str(), NULL, NULL, graph);
   if (ret < 0)
      return ret;
   if ((ret = av_opt_set_int(ofilter.filter, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
      return ret;

   if (ost.audio_channels_map.size())
   {
      std::ostringstream pan_buf;
      pan_buf << "0x" << av_get_default_channel_layout((int)ost.audio_channels_map.size());
      for (int i = 0; i < ost.audio_channels_map.size(); i++)
         if (ost.audio_channels_map[i] != -1)
            pan_buf << "|c" << i << "=c" << ost.audio_channels_map[i];

      if (auto_insert_filter("-map_channel", "pan", pan_buf.str(),pad_idx,last_filter)<0)
         return ret;
   }

   AVCodecContext *codec = ost.enc_ctx;

   if (codec->channels && !codec->channel_layout)
      codec->channel_layout = av_get_default_channel_layout(codec->channels);

   std::string sample_fmts = ost.choose_sample_fmts();
   std::string sample_rates = ost.choose_sample_rates();
   std::string channel_layouts = ost.choose_channel_layouts();
   if (sample_fmts.size() || sample_rates.size() || channel_layouts.size())
   {
      AVFilterContext *format;
      std::ostringstream args;

      if (sample_fmts.size())
         args << "sample_fmts=" << sample_fmts << ":";
      if (sample_rates.size())
         args << "sample_rates=" << sample_rates << ":";
      if (channel_layouts.size())
         args << "channel_layouts=" << channel_layouts << ":";

      name.str("");
      name << "audio format for output stream " << ost.file.index << ":" << ost.index;
      ret = avfilter_graph_create_filter(&format, avfilter_get_by_name("aformat"), name.str().c_str(), args.str().c_str(), NULL, graph);
      if (ret < 0)
         return ret;

      ret = avfilter_link(last_filter, pad_idx, format, 0);
      if (ret < 0)
         return ret;

      last_filter = format;
      pad_idx = 0;
   }

   OutputFile &of = ost.file;
   if (ost.apad.size() && of.shortest)
   {
      // find the first video output stream
      unsigned int i;
      for (i = 0; i < of.ctx->nb_streams; i++)
         if (of.ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            break;

      if (i < of.ctx->nb_streams)
      {
         if (auto_insert_filter("-apad", "apad", ost.apad, pad_idx, last_filter) < 0)
            return ret;
      }
   }

   name.str("");
   name << "trim for output stream " << ost.file.index << ":" << ost.index;
   ret = insert_trim(of.start_time, of.recording_time, last_filter, pad_idx, name.str());
   if (ret < 0)
      return ret;

   if ((ret = avfilter_link(last_filter, pad_idx, ofilter.filter, 0)) < 0)
      return ret;

   return 0;
}

int FilterGraph::configure_output_filter(OutputFilter &ofilter, AVFilterInOut *out)
{
   ofilter.describe_filter_link(out);

   if (!ofilter.ost)
      throw ffmpegException("Filter " + std::string(ofilter.name) + " has an unconnected output");

   switch (avfilter_pad_get_type(out->filter_ctx->output_pads, out->pad_idx))
   {
   case AVMEDIA_TYPE_VIDEO:
      return configure_output_video_filter(ofilter, out);
   case AVMEDIA_TYPE_AUDIO:
      return configure_output_audio_filter(ofilter, out);
   default:
      av_assert0(0);
   }
}

int FilterGraph::init_complex_filtergraph(const InputFiles &input_files, InputStreamPtrs &input_streams)
{
   // takes fg->graph_desc,

   /* this graph is only used for determining the kinds of inputs
     * and outputs we have, and is discarded on exit from this function */
   AvFilterGraphPtr avf_graph(avfilter_graph_alloc(), delete_avfilter_graph);
   if (!avf_graph)
      return AVERROR(ENOMEM);

   AvFilterInOutPtr avf_inputs(NULL, delete_avfilter_inout);
   AVFilterInOut *avf_inputs_temp, *avf_outputs, *avf_cur;
   int ret = avfilter_graph_parse2(avf_graph.get(), graph_desc.c_str(), &avf_inputs_temp, &avf_outputs);
   if (avf_inputs_temp)
      avf_inputs.reset(avf_inputs_temp);
   if (ret < 0)
      return ret;

   for (avf_cur = avf_inputs.get(); avf_cur; avf_cur = avf_cur->next)
      init_input_filter(avf_cur, input_streams);

   for (avf_cur = avf_outputs; avf_cur;)
   {
      outputs.emplace_back(*this,avf_cur,avfilter_pad_get_type(avf_cur->filter_ctx->output_pads, avf_cur->pad_idx));

      // go to next output filter, then remove the between-output linkage on the outputs element just created
      avf_cur = avf_cur->next;
      outputs.back().out_tmp->next = NULL;
   }

   return ret;
}

void FilterGraph::init_input_filter(AVFilterInOut *in, InputStreamPtrs &input_streams)
// static void init_input_filter(FilterGraph *fg, AVFilterInOut *in)
{
   std::ostringstream msg;

   AVMediaType type = avfilter_pad_get_type(in->filter_ctx->input_pads, in->pad_idx);

   // TODO: support other filter types
   if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO)
      throw ffmpegException("Only video and audio filters supported currently.");

   InputStreamPtrs::iterator ist = input_streams.begin();
   if (in->name) // filter input stream name is given, find a matching stream
   {
      char *p;
      int file_idx = strtol(in->name, &p, 0); // p contains the stream specifier string

      if (file_idx < 0 || file_idx > input_streams.back()->file.index)
      {
         msg << "Invalid file index " << file_idx << " in filtergraph description " << graph_desc << ".";
         throw ffmpegException(msg.str());
      }

      // traverse all input streams
      for (; ist < input_streams.end(); ist++)
      {
         if ((*ist)->file.index != file_idx)
            continue;

         AVMediaType stream_type = (*ist)->get_stream_type();
         if (stream_type != type && !(stream_type == AVMEDIA_TYPE_SUBTITLE && type == AVMEDIA_TYPE_VIDEO /* sub2video hack */))
            continue;
         if ((*ist)->check_stream_specifier(std::string(*p == ':' ? p + 1 : p)) == 1)
            break;
      }

      // fatal error if no match found
      if (ist == input_streams.end())
      {
          msg << "Stream specifier '" << p << "' in filtergraph description " << graph_desc << " matches no streams.";
          throw ffmpegException(msg.str());
      }
   }
   else
   {
      /* find the first unused stream of corresponding type */
      for (; ist < input_streams.end(); ist++)
      {
         if ((*ist)->unused_stream(type))
            break;
      }
      if (ist == input_streams.end())
      {
         msg << "Cannot find a matching stream for unlabeled input pad " << in->pad_idx << " on filter " << in->filter_ctx->name;
         throw ffmpegException(msg.str());
      }
   }

   // create a new InputFilter object
   inputs.emplace_back(*this, **ist);
}

/////////////////////////////////////////////////////////////////////////////////////////////

void OutputFilter::init_output_filter(OutputFile &ofile, OptionsContext &o)
{
   switch (type)
   {
   case AVMEDIA_TYPE_VIDEO:
      ost = &ofile.new_video_stream(o, NULL);
      break;
   case AVMEDIA_TYPE_AUDIO:
      ost = &ofile.new_audio_stream(o, NULL);
      break;
   default:
      throw ffmpegException("Only video and audio filters are supported currently.");
   }

   ost->source_ist = NULL; // not directly from input stream
   ost->filter = this;

   if (ost->stream_copy)
   {
      std::ostringstream msg;
      msg << "Streamcopy requested for output stream " << ost->file.index << ":" << ost->index << ", "
          << "which is fed from a complex filtergraph. Filtering and streamcopy cannot be used together.";
      throw ffmpegException(msg.str());
   }

   if (ost->avfilter.size() && (ost->filters.size() || ost->filters_script.size()))
   {
      const std::string opt = ost->filters.size() ? "-vf/-af/-filter" : "-filter_script";
      std::ostringstream msg;
      msg << (ost->filters.size() ? "Filtergraph" : "Filtergraph script") << " ''"
          << (ost->filters.size() ? ost->filters : ost->filters_script)
          << "' was specified through the " << opt << " option for output stream "
          << ost->file.index << ":" << ost->index << ", which is fed from a complex filtergraph." << std::endl
          << opt << " and -filter_complex cannot be used together for the same stream.";
      throw ffmpegException(msg.str());
   }
}
