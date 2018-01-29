#pragma once

#include "ffmpegStreamInput.h"
#include "ffmpegStreamOutput.h"

// #include "mexClassHandler.h"
// #include "ffmpegPtrs.h"
// #include "ffmpegAvRedefine.h"
// #include "ffmpegAVFramePtrBuffer.h"
// #include "ffmpegFrameBuffers.h"

extern "C" {
#include <libavfilter/avfiltergraph.h>
// #include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
// #include <libavutil/pixdesc.h>
}

#include <vector>

namespace ffmpeg
{

struct VideoFilterParams
{
  int width, height;
  AVRational time_base;
  // AVRational frame_rate;
  AVRational sample_aspect_ratio;
  AVPixelFormat format;
};

class AudioFilterParams
{
protected:
  AVSampleFormat format;
  AVRational time_base;
  // int sample_rate;
  int channels;
  uint64_t channel_layout;
};

/**
 * 
 *
 */
class FilterBase
{
public:
  FilterBase(FilterGraph &parent);

  virtual AVFilterContext *configure(const std::string &fname, const std::string &name = "");
  virtual void unconfigure(const bool deep=false);
  virtual void link(AVFilterContext *other, unsigned otherpad) = 0;

  AVFilterContext *filter; // filter object

protected:
  virtual std::string generate_args();

  FilterGraph &graph;

  BaseStream *st; // non-NULL if associated to an AVStream

  // std::string name;
  AVMediaType type; // AVMEDIA_TYPE_SUBTITLE for sub2video
  std::string args; // argument to create the filter

};

class InputFilter : public BaseFilter
{
public:
  InputFilter(FilterGraph &fg, InputStream &ist);
  InputFilter(FilterGraph &fg, IAVFrameSource &buf);

  virtual void link(AVFilterContext *other, unsigned otherpad);

protected:
  IAVFrameSource *src;
  AVBufferRef *hw_frames_ctx;
};

class InputVideoFilter : public InputFilter, private VideoFilterParams
{
public:
  InputVideoFilter(FilterGraph &fg, InputVideoStream &ist);     // connected to an FFmpeg stream
  InputVideoFilter(FilterGraph &fg, IAVFrameBufferSource &buf); // connected to a buffer (data from non-FFmpeg source)
  AVFilterContext *configure(const std::string &name = "");
  std::string generate_args();
  private:
  int sws_flags;
  void get_stream_parameters();
  };
class InputAudioFilter : public InputFilter, private AudioFilterParams
{
public:
  InputAudioFilter(FilterGraph &fg, InputAudioStream &ist);
  InputAudioFilter(FilterGraph &fg, IAVFrameBufferSource &buf); // connected to a buffer (data from non-FFmpeg source)
  AVFilterContext *configure(const std::string &name = "");
  std::string generate_args();

private:
  void get_stream_parameters();
};

class OutputFilter : public BaseFilter
{
public:
  OutputFilter(FilterGraph &fg, OutputStream &ost);       // connected to an FFmpeg stream
  OutputFilter(FilterGraph &fg, IAVFrameBufferSink &buf); // connected to a buffer (data from non-FFmpeg source)

  virtual void link(AVFilterContext *other, unsigned otherpad)
  {
    if (avfilter_link(other, otherpad, filter, 0) < 0)
      throw ffmpegException("Failed to link OutputFilter.");
  }

protected:
  IAVFrameSink *sink;

  /* temporary storage until stream maps are processed */
  AVFilterInOut *out_tmp;
};

class OutputVideoFilter : public OutputFilter, public VideoFilterParams
{
public:
  OutputVideoFilter(FilterGraph &fg, OutputStream &ost);       // connected to an FFmpeg stream
  OutputVideoFilter(FilterGraph &fg, IAVFrameBufferSink &buf); // connected to a buffer (data from non-FFmpeg source)
  AVFilterContext *configure(const std::string &name = "");

  std::string choose_pix_fmts();
};

class OutputAudioFilter : public OutputFilter, public AudioFilterParams
{
public:
  OutputAudioFilter(FilterGraph &fg, OutputStream &ost);       // connected to an FFmpeg stream
  OutputAudioFilter(FilterGraph &fg, IAVFrameBufferSink &buf); // connected to a buffer (data from non-FFmpeg source)
  AVFilterContext *configure(const std::string &name = "");

  std::string choose_sample_fmts();
  std::string choose_sample_rates();
  std::string choose_channel_layouts();
};

typedef std::vector<InputFilter *> InputFilterPtrs;
typedef std::vector<OutputFilter *> OutputFilterPtrs;
}
