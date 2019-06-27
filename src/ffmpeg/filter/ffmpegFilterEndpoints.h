#pragma once

#include "ffmpegFilterBase.h"
#include "../ffmpegStream.h"
#include "../ffmpegMediaStructs.h"
#include "../ffmpegException.h"

extern "C" {
// #include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
// #include <libavfilter/avfiltergraph.h>
#include <libavutil/frame.h>
}

namespace ffmpeg
{
namespace filter
{
class EndpointBase : public Base
{
public:
  // AVMediaType type;   AVRational time_base
  EndpointBase(Graph &parent);
  virtual ~EndpointBase();
  
  /**
   * \brief Purge AVFilterContext
   * 
   * When the associated AVFilterGraph is destroyed, \ref context becomes invalid. ffmpeg::filter::Graph
   * calls this function to make its filters deassociate invalidated AVFilterContexts.
   */
  virtual void purge();

  virtual int processFrame() = 0;

  /**
   * /brief   Retrieve the prefilter chain description
   * 
   * \returns Prefilter chain description string
   * \thrpws Exception if fails to parse
   */
  const std::string &setPrefilter();

  /**
   * /brief   Register the prefilter chain description
   * 
   * This function takes a prefilter chain description, creates a dummy filter
   * graph to verify that it is a SISO graph.
   * 
   * \param[in] desc Prefilter chain description
   * \thrpws Exception if fails to parse
   */
  virtual void setPrefilter(const std::string &desc);
  
  /**
   * \brief Links the filter to another filter
   * 
   * A pure virtual function to link this filter with another
   * 
   * \param other[inout]  Context of the other filter
   * \param otherpad[in]  The connector pad of the other filter
   * \param pad[in]  [optional, default:0] The connector pad of this filter
   * \param issrc[in]  [optional, default:true] True if this filter is the source
   * 
   * \throws Exception if either filter context is not ready.
   * \throws Exception if filter contexts are not for the same filtergraph.
   * \throws Exception if failed to link.
   */
  virtual void link(AVFilterContext *other, const unsigned otherpad, const unsigned pad=0, const bool issrc=true);

protected:

  /**
   * /brief   Add prefilter graph to the filter graph
   * 
   * This function add specified prefilter graph to the existing filter graph,
   * link to the endpoint filter \ref ep, and returns the unlinked end of the
   * prefilter chain to the caller. If no prefilter is specified, it just returns
   * the endpoint filter.
   * 
   * \param[in] ep Endpoint filter context to attach the prefilter to
   * \param[in] issrc True if \ref ep is a source filter
   * \returns \ref ep if no prefilter or the unlinked filter of the prefilter chain
   */
  AVFilterContext* configure_prefilter(bool issrc);

  std::string prefilter_desc; // description of a simple filter graph to run immediate to the endpoint filter
  AVFilterContext *prefilter_context; // context object
  int prefilter_pad;
};
}
}
