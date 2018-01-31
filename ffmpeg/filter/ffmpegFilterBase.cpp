#include "ffmpegFilterBase.h"

#include "ffmpegFilterGraph.h"
#include "../ffmpegException.h"

using namespace ffmpeg::filter;

Base::Base(Graph &parent) : graph(parent), context(NULL) {}
Base::~Base() {}
void Base::destroy(const bool deep)
{
  if (context && deep)
    avfilter_free(context);
  context = NULL;
  args.clear();
}

void Base::link(AVFilterContext *other, const unsigned otherpad, const unsigned pad, const bool issrc)
{
  if (!context)
    throw ffmpegException("Filter context has not been configured.");
  if (!other)
    throw ffmpegException("The other filter context not given (NULL).");
  if (context->graph != other->graph)
    throw ffmpegException("Filter contexts must be for the same AVFilterGraph.");

  int ret;
  if (issrc)
    ret = avfilter_link(context, pad, other, otherpad);
  else
    ret = avfilter_link(other, otherpad, context, pad);
  if (ret < 0)
    throw ffmpegException("Failed to link filters.");
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
    throw ffmpegException("Object already has configured an AVFilter.");

  std::string new_args = generate_args();

  if (avfilter_graph_create_filter(&context,
                                   avfilter_get_by_name(fname.c_str()),
                                   name.c_str(), new_args.c_str(), NULL, graph.getAVFilterGraph()) < 0)
    throw ffmpegException("Failed to create a %s context.", fname.c_str());

  // store the argument
  args = new_args;

  return context;
}

std::string Base::generate_args() { return ""; }
