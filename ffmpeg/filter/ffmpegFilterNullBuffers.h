#pragma once

#include "ffmpegFilterBase.h"
#include "ffmpegFilterGraph.h"

extern "C" {
// #include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
// #include <libavfilter/avfiltergraph.h>
#include <libavfilter/avfilter.h>
}

namespace ffmpeg
{
namespace filter
{
class NullVideoSource : public Base
{
public:
  NullVideoSource(Graph &parent) : Base(parent) {}
  virtual ~NullVideoSource() {}

  AVFilterContext *configure(const std::string &name = "")
  {
    create_context("nullsrc", name);
    return context;
  }
};
class NullVideoSink : public Base
{
public:
  NullVideoSink(Graph &parent) : Base(parent) {}
  virtual ~NullVideoSink() {}

  AVFilterContext *configure(const std::string &name = "")
  {
    create_context("nullsink", name);
    return context;
  }
};
class NullAudioSource : public Base
{
public:
  NullAudioSource(Graph &parent) : Base(parent) {}
  virtual ~NullAudioSource() {}

  AVFilterContext *configure(const std::string &name = "")
  {
    create_context("anullsrc", name);
    return context;
  }
};
class NullAudioSink : public Base
{
public:
  NullAudioSink(Graph &parent) : Base(parent) {}
  virtual ~NullAudioSink() {}

  AVFilterContext *configure(const std::string &name = "")
  {
    create_context("anullsink", name);
    return context;
  }
};
}
}
