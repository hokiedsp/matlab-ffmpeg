#include "ffmpegFilters.h"

#include "ffmpegException.h"

#include <sstream> // std::stringstream
#include <iomanip> // std::stringstream

using namespace ffmpeg;

///////////////////////////////////////////////////////////

FilterBase::FilterBase(FilterGraph &parent) : graph(parent), filter(NULL), st(NULL) {}

AVFilterContext *FilterBase::configure(const std::string &fname, const std::string &name)
{
  if (filter)
    throw ffmpegException("Object already has configured an AVFilter.");

  std::string new_args = generate_args();

  if (avfilter_graph_create_filter(&filter,
                                   avfilter_get_by_name(fname.c_str()),
                                   name.c_str(), new_args.c_str(), NULL, graph) < 0)
    throw ffmpegException("Failed to create a filter.");

  // store the argument
  args = new_args;

  return filter;
};

virtual void FilterBase::unconfigure(const bool deep)
{
  if (filter && deep)
    avfilter_free(filter);
  filter = NULL;
};

std::string FilterBase::generate_args() { return ""; }

///////////////////////////////////////////////////////////

InputFilter(FilterGraph &fg, InputStream &ist) : FilterBase(fg), st(&ist), src(ist.getBuffer()), hw_frames_ctx(NULL)
{
  if (!src)
    throw ffmpegException("Attempted to construct InputFilter with an InputStream object without a buffer.");

  // Grab the video parameters from the stream. Will be checked again when configured
  get_stream_parameters();
}

InputFilter(FilterGraph &fg, IAVFrameSource &buf, VideoFilterParams &params)
    : FilterBase(fg), VideoFilterParams(params), src(&buf), hw_frames_ctx(NULL)
{
}

virtual void link(AVFilterContext *other, unsigned otherpad)
{
  if (avfilter_link(filter, 0, other, otherpad) < 0)
    throw ffmpegException("Failed to link InputFilter.");
}

/////////////////////////////

InputVideoFilter::InputVideoFilter(FilterGraph &fg, InputStream &ist)
    : InputFilter(fg, ist), type(AVMEDIA_TYPE_VIDEO), sws_flags(0) {}
InputVideoFilter::InputVideoFilter(FilterGraph &fg, IAVFrameBufferSource &buf)
    : InputFilter(fg, buf), type(AVMEDIA_TYPE_VIDEO), sws_flags(0) {}

AVFilterContext *InputVideoFilter::configure(const std::string &name)
{
  // configure the filter
  FilterBase::configure("buffer", name);

  // also send in the hw_frame_ctx  (if given)
  AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();
  if (!par)
    throw ffmpegException("Failed during av_buffersrc_parameters_alloc() call.");

  memset(par, 0, sizeof(*par));
  par->format = AV_PIX_FMT_NONE;
  par->hw_frames_ctx = hw_frames_ctx;
  ret = av_buffersrc_parameters_set(filter, par);
  av_freep(&par);
  if (ret < 0)
    throw ffmpegException("Failed to call av_buffersrc_parameters_set().");

  return filter;
}

void InputVideoFilter::get_stream_parameters()
{
  // if not from AVStream, nothing to update
  if (!st)
    return;

  InputVideoStream *ist = (InputVideoStream *)st;

  width = ist->getWidth();
  height = ist->getHeight();
  time_base = ist->getTimeBase();
  sample_aspect_ratio = ist->getSAR();
  format = ist->getPixelFormat();
  sws_flags = SWS_BILINEAR + (ist->getCodecFlags(AV_CODEC_FLAG_BITEXACT) ? SWS_BITEXACT : 0);
}

std::string InputVideoFilter::generate_args();
{
  // AVRational sample_aspect_ratio;
  sstream AVBPrint args;

  // update object parameters with AVStream parameter
  get_stream_parameters();

  // make sure SAR is valid
  if (!sample_aspect_ratio.den)
    sample_aspect_ratio = (AVRational){0, 1};

  std::stringstream sout;
  sout << "video_size=" << width << 'x' << height << ':'
       << "pix_fmt=" << av_get_pix_fmt_name(format) << ':'
       << "time_base=" << time_base.num << '/' << time_base.den << ':'
       << "pixel_aspect=" << sample_aspect_ratio.num << '/' << sample_aspect_ratio.den << ':'
       << "sws_param=flags=" << sws_flags;

  return sout.str();
}

/////////////////////////////

InputAudioFilter::InputAudioFilter(FilterGraph &fg, InputAudioStream &ist)
    : InputFilter(fg, ist), type(AVMEDIA_TYPE_AUDIO) {}
InputAudioFilter::InputAudioFilter(FilterGraph &fg, IAVFrameBufferSource &buf)
    : InputFilter(fg, buf), type(AVMEDIA_TYPE_AUDIO) {}
AVFilterContext *InputAudioFilter::configure(const std::string &name)
{
  // configure the filter
  FilterBase::configure("abuffer", name);
}

void InputAudioFilter::get_stream_parameters()
{
  // if not from AVStream, nothing to update
  if (!st)
    return;

  InputAudioStream *ist = (InputAudioStream *)st;

  time_base = ist->getTimeBase();
  format = ist->getSampleFormat();
  channels = ist->getChannels();
  channel_layout = ist->getChannelLayout();
}

std::string InputAudioFilter::generate_args()
{
  std::stringstream sout;
  sout << "time_base=" << time_base.num << 'x' << time_base.den << ':'
       << "sample_fmt=" << av_get_sample_fmt_name(format) << ':';
  if (channel_layout)
    sout << "channel_layout=" << std::setbase(16) << channel_layout;
  else
    sout << "channels=" << channels;
  return sout.str();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
OutputFilter::OutputFilter(FilterGraph &fg, OutputStream &ost)
    : FilterBase(fg), st(&ost), sink(ost.getBuffer()) {}
OutputFilter::OutputFilter(FilterGraph &fg, IAVFrameBufferSink &buf)
    : FilterBase(fg), st(&ist), src(ist.getBuffer()) {}

void OutputFilter::link(AVFilterContext *other, unsigned otherpad)
{
  if (avfilter_link(other, otherpad, filter, 0) < 0)
    throw ffmpegException("Failed to link OutputFilter.");
}

////////////////////////////////
OutputVideoFilter::OutputVideoFilter(FilterGraph &fg, OutputVideoStream &ost)
    : OutputFilter(fg, ost), type(AVMEDIA_TYPE_VIDEO) {}
OutputVideoFilter::OutputVideoFilter(FilterGraph &fg, IAVFrameBufferSink &buf)
    : OutputFilter(fg, buf), type(AVMEDIA_TYPE_VIDEO) {}
AVFilterContext *OutputVideoFilter::configure(const std::string &name)
{ // configure the filter
  FilterBase::configure("buffersink", name);
}

////////////////////////////////
OutputAudioFilter::OutputAudioFilter(FilterGraph &fg, OutputAudioStream &ost)
    : OutputFilter(fg, ost), type(AVMEDIA_TYPE_AUDIO) {}
OutputAudioFilter::OutputAudioFilter(FilterGraph &fg, IAVFrameBufferSink &buf)
    : OutputFilter(fg, buf), type(AVMEDIA_TYPE_AUDIO) {}
AVFilterContext *OutputAudioFilter::configure(const std::string &name)
{
  // configure the filter
  FilterBase::configure("abuffersink", name);
  av_opt_set_int(filter, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN));
}
