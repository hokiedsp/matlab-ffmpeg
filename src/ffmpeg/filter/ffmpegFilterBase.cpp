#include "ffmpegFilterBase.h"

#include "ffmpegFilterGraph.h"
#include "../ffmpegException.h"

using namespace ffmpeg::filter;

Base::Base(Graph &parent) : graph(parent), context(NULL) {}
Base::~Base() { if (context) avfilter_free(context); }

void Base::purge()
{
  context = NULL;
}

void Base::link(AVFilterContext *other, const unsigned otherpad, const unsigned pad, const bool issrc)
{
  if (!context)
    throw Exception("Filter context has not been configured.");
  if (!other)
    throw Exception("The other filter context not given (NULL).");
  if (context->graph != other->graph)
    throw Exception("Filter contexts must be for the same AVFilterGraph.");

  int ret;
  if (issrc)
    ret = avfilter_link(context, pad, other, otherpad);
  else
    ret = avfilter_link(other, otherpad, context, pad);
  if (ret < 0)
  {
    if (ret == AVERROR(EINVAL))
      throw Exception("Failed to link filters (invalid parameter).");
    else if (ret == AVERROR(ENOMEM))
      throw Exception("Failed to link filters (could not allocate memory).");
    else
      throw Exception("Failed to link filters.");
  }
}
void Base::link(Base &other, const unsigned otherpad, const unsigned pad, const bool issrc)
{
  link(other.context, otherpad, pad, issrc);
}
std::string Base::getName() const { return context ? context->name : ""; }

AVFilterContext* Base::getAVFilterContext() const { return context; }

AVFilterContext *Base::create_context(const std::string &fname, const std::string &name)
{
  if (context)
    throw Exception("[ffmpeg::filter::Base::create_context] Object already has configured an AVFilter.");

  // get the arguments for the filter
  std::string new_args = generate_args();

  // create the filter
  if (avfilter_graph_create_filter(&context,
                                   avfilter_get_by_name(fname.c_str()),
                                   name.c_str(), new_args.c_str(), NULL, graph.getAVFilterGraph()) < 0)
    throw Exception("[ffmpeg::filter::Base::create_context] Failed to create a %s context with argument:\n\t%s.", fname.c_str(), new_args.c_str());

  return context;
}

std::string Base::generate_args() { return ""; }
