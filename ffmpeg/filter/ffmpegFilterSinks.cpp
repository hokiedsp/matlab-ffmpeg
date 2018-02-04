#include "ffmpegFilterSinks.h"

#include "ffmpegFilterGraph.h"
#include "../ffmpegException.h"

extern "C" {
 #include <libavfilter/buffersink.h>
 #include <libavutil/opt.h>
}

#include <sstream> // std::stringstream
#include <iomanip>

using namespace ffmpeg;
using namespace ffmpeg::filter;

///////////////////////////////////////////////////////////
SinkBase::SinkBase(Graph &fg, AVMediaType mediatype) : EndpointBase(fg, mediatype), sink(NULL), ena(false) {}
SinkBase::SinkBase(Graph &fg, OutputStream &ost, AVMediaType mediatype)
    : EndpointBase(fg, ost, mediatype), sink(dynamic_cast<IAVFrameSink *>(ost.getBuffer()))
    {
        if (!sink)
          throw ffmpegException("Attempted to construct ffmpeg::filter::*Sink object from an OutputStream object without a buffer.");
    }
SinkBase::SinkBase(Graph &fg, IAVFrameSink &buf, AVMediaType mediatype)
    : EndpointBase(fg, mediatype), sink(&buf) {}

AVFilterContext *SinkBase::configure(const std::string &name)
{ // configure the AVFilterContext
  ena = true;
  return context;
}

void SinkBase::link(AVFilterContext *other, const unsigned otherpad, const unsigned pad, const bool issrc)
{
  if (issrc || pad > 0)
    throw ffmpegException("Sink filter does not have a input pad and has only one output pad.");

  Base::link(other, otherpad, pad, issrc);
}

int SinkBase::processFrame()
{
  if (!sink)
    throw ffmpegException("[SinkBase::processFrame] AVFrame sink buffer has not been set.");

  AVFrame *frame = NULL;
  int ret = av_buffersink_get_frame(context, frame);
  bool eof = (ret != AVERROR_EOF);
  if (ret==0 || eof)
  {
    sink->push(eof ? NULL : frame);
    if (eof) ena = false;
  }

  return ret;
}

////////////////////////////////
VideoSink::VideoSink(Graph &fg)  : SinkBase(fg, AVMEDIA_TYPE_VIDEO) {}
VideoSink::VideoSink(Graph &fg, OutputStream &ost)  : SinkBase(fg, dynamic_cast<OutputVideoStream&>(ost), AVMEDIA_TYPE_VIDEO) {}
VideoSink::VideoSink(Graph &fg, IAVFrameSink &buf) : SinkBase(fg, buf, AVMEDIA_TYPE_VIDEO) {}
AVFilterContext *VideoSink::configure(const std::string &name)
{ // configure the AVFilterContext
  create_context("buffersink", name);
  SinkBase::configure();
  return context;
}

void VideoSink::sync()
{
  format = (AVPixelFormat)av_buffersink_get_format(context);
  width = av_buffersink_get_w(context);
  height = av_buffersink_get_h(context);
  time_base = av_buffersink_get_time_base(context);
  sample_aspect_ratio = av_buffersink_get_sample_aspect_ratio(context);
}

std::string VideoSink::choose_pix_fmts()
// static char *choose_pix_fmts(SinkBase *ofilter)
{
  if (!st)
    return "";

  OutputVideoStream *ost = dynamic_cast<OutputVideoStream *>(st);

  std::string rval;

  AVPixelFormats fmts = ost->choose_pix_fmts();

  if (size(fmts) == 1 && fmts[0] == AV_PIX_FMT_NONE) // use as propagated
  {
    avfilter_graph_set_auto_convert(graph.getAVFilterGraph(), AVFILTER_AUTO_CONVERT_NONE);
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

////////////////////////////////
AudioSink::AudioSink(Graph &fg) : SinkBase(fg, AVMEDIA_TYPE_AUDIO) {}
AudioSink::AudioSink(Graph &fg, OutputStream &ost) : SinkBase(fg, dynamic_cast<OutputAudioStream&>(ost), AVMEDIA_TYPE_AUDIO) {}
AudioSink::AudioSink(Graph &fg, IAVFrameSink &buf) : SinkBase(fg, buf, AVMEDIA_TYPE_AUDIO) {}
AVFilterContext *AudioSink::configure(const std::string &name)
{
  // clears ena flag
  SinkBase::configure();

  // configure the filter
  create_context("abuffersink", name);
  av_opt_set_int(context, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN);
  return context;
}

void AudioSink::sync()
{
  format = (AVSampleFormat)av_buffersink_get_format(context);
  time_base = av_buffersink_get_time_base(context);
  channel_layout = av_buffersink_get_channel_layout(context);
  channels = av_buffersink_get_channels(context);

  // if linked to a stream
  if (st)
  {
    const AVCodec *enc = st->getAVCodec();
    if (!enc)
      throw ffmpegException("Encoder (codec %s) not found for an output stream",
                            avcodec_get_name(st->getAVStream()->codecpar->codec_id));

    if (!(enc->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
      av_buffersink_set_frame_size(context, st->getCodecFrameSize());
  }
}

/* Define a function for building a string containing a list of
 * allowed formats. */
#define DEF_CHOOSE_FORMAT(suffix, type, var, supported_list, none, get_name) \
  \
std::string AudioSink::choose_##suffix()                             \
  \
{                                                                         \
    if (var != none)                                                         \
    {                                                                        \
      get_name(var);                                                         \
      return name;                                                           \
    }                                                                        \
/*     else if (supported_list)                                                 \
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
 */    else                                                                     \
      return "";                                                             \
  \
}

#define GET_SAMPLE_FMT_NAME(sample_fmt)\
    std::string name = av_get_sample_fmt_name(sample_fmt);

#define GET_SAMPLE_RATE_NAME(rate)\
    std::string = std::to_string(rate);

#define GET_CH_LAYOUT_NAME(ch_layout)           \
  \
std::string name;                               \
  {                                             \
    std::stringstream sout;                     \
    sout << "0x" << std::setbase(16) << channel_layout; \
    \
name = sout.str();                              \
  \
}

DEF_CHOOSE_FORMAT(sample_fmts, AVSampleFormat, format, formats,
                  AV_SAMPLE_FMT_NONE, GET_SAMPLE_FMT_NAME)

// DEF_CHOOSE_FORMAT(sample_rates, int, sample_rate, sample_rates, 0,
//                   GET_SAMPLE_RATE_NAME)

DEF_CHOOSE_FORMAT(channel_layouts, uint64_t, channel_layout, channel_layouts, 0,
                  GET_CH_LAYOUT_NAME)
