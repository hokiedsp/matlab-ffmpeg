#pragma once

extern "C"
{
// #include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
#define __STDC_CONSTANT_MACROS
#include <libavfilter/avfilter.h>
#include <libavutil/pixdesc.h>
}

#include <string>

namespace ffmpeg
{
namespace filter
{

class Graph;

/**
 * \brief Base class for all classes defined in ffmpeg::context, except for ffmpeg::context::Graph
 * 
 * Base class for all classes defined in ffmpeg::context. It encapsulates AVFilterContext to 
 * provide a centralized way to configure FFmpeg filters. 
 * 
 * \note This class creates the AVFilterContext and stores its pointer in  \ref context. However, 
 *       it does not destory the context when the class object is destructed. The destruction of 
 *       the context is depending on its parent ffmpeg::context::Graph object, which is linked with
 *       its \ref graph member variable.
 */
class Base
{
public:
  /**
   * \brief ffmpeg::context::Base Constructor
   * 
   * Constructs an ffmpeg::context::Base class object
   * \param parent  Parent ffmpeg::context::Graph object
   */
  Base(Graph &parent);

  /**
   * \brief ffmpeg::context::Base Destructor
   * 
   * Destructs an ffmpeg::context::Base class object
   */
  virtual ~Base();

  /**
   * \brief Generate its internal AVFilterContext from the current object states
   * 
   * A pure virtual function to create a new AVFilterContext and stores it in \ref 
   * context member variable. The context's name is given as an input while its 
   * parameters are configured based on the states of the (derived) class object.
   * 
   * \param name[in]  Name of the created context
   * \returns Pointer to the created AVFilterContext (i.e., the copy of the internal \ref context 
   *          variable)
   * 
   * \note Derived classes must implement this function with the aid of \ref create_context member
   *       function.
   * 
   * \throws Exception if failed to create the new context.
   */
  virtual AVFilterContext *configure(const std::string &name = "") = 0;

  /**
   * \brief Purge AVFilterContext
   * 
   * When the associated AVFilterGraph is destroyed, \ref context becomes invalid. ffmpeg::filter::Graph
   * calls this function to make its filters deassociate invalidated AVFilterContexts.
   */
  virtual void purge();

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
  virtual void link(AVFilterContext *other, const unsigned otherpad, const unsigned pad = 0, const bool issrc = true);

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
   * \throws Exception see \ref link(AVFilterContext*, const unsigned, const unsigned, const bool)
   */
  void link(Base &other, const unsigned otherpad, const unsigned pad = 0, const bool issrc = true);

  /**
   * \brief Name of this filter instance
   * 
   * Name of this filter object, only available after its context is set
   * 
   * \returns Filter name or empty string if not configured
   */
  std::string getName() const;

  /**
   * \brief Underlying FFmpeg AVFilterContext object
   * 
   * Returns the pointer to the AVFilterContext object if the filter has been configured (\ref configure())
   * 
   * \returns Pointer to the AVFilterContext object or NULL if not yet configured.
   */
  AVFilterContext *getAVFilterContext() const;

protected:
  /**
   * \brief Generate its internal AVFilterContext from the current object state
   * 
   * Creates a new AVFilterContext via avfilter_graph_create_filter() call. Its context 
   * parameter string is internally generated via \ref generate_args protected virtual member 
   * function.
   * 
   * \param fname[in] AVFilter name e.g. "buffer","abuffer", and "trim"
   * \param name[in]  Name of the created context context
   * \returns Pointer to the created AVFilterContext (i.e., the copy of the internal \ref context 
   *          variable)
   * 
   * \note This function is expected to be overloaded by derived classes without \ref fname argument
   *       and the overloaded function should call this function with the name of the AVFilter under
   *       implementation.
   * 
   * \throws Exception if \ref context member variable is already non-null or fails to 
   *         create the new AVFilterContext.
   */
  AVFilterContext *create_context(const std::string &fname, const std::string &name);

  /**
   * \brief Generates filter parameter argument string
   * 
   * If the encapsulated AVFilter takes parameters, overload this function to generate
   * the parameter string from the states of the derived class object.
   * 
   * /returns AVFilter parameter argument string to be passed onto avfilter_graph_create_filter
   */
  virtual std::string generate_args();

  Graph &graph;             // associated filtergraph object (does not change during the life of the object)
  AVFilterContext *context; // context object
};
} // namespace filter
} // namespace ffmpeg
