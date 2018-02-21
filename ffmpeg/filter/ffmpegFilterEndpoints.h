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
class EndpointBase : public Base, public MediaHandler, public AVFrameHandler
{
public:
  // AVMediaType type;   AVRational time_base
  EndpointBase(Graph &parent, const AVMediaType type, const AVRational &tb = {0, 0});
  EndpointBase(Graph &parent, const IMediaHandler &mdev);
  virtual ~EndpointBase();

  virtual int processFrame() = 0;

  /**
   * /brief   Retrieve the prefilter chain description
   * 
   * \returns Prefilter chain description string
   * \thrpws ffmpegException if fails to parse
   */
  const std::string &setPrefilter();

  /**
   * /brief   Register the prefilter chain description
   * 
   * This function takes a prefilter chain description, creates a dummy filter
   * graph to verify that it is a SISO graph.
   * 
   * \param[in] desc Prefilter chain description
   * \thrpws ffmpegException if fails to parse
   */
  virtual void setPrefilter(const std::string &desc);

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
  AVFilterContext* configure_prefilter(AVFilterContext *ep, bool issrc);

  std::string prefilter_desc; // description of a simple filter graph to run immediate to the endpoint filter
  int prefilter_pad;
};
}
}
