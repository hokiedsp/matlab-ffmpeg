#pragma once

#include <string>
#include <vector>
#include <functional>

extern "C" {
#include <libavfilter/avfilter.h> // for AVFilterContext
#include <libavformat/avformat.h> // for AVFormatContext
#include <libavutil/avutil.h>     // for AVMediaType
}

#include "ffmpegBase.h"
#include "ffmpegOptionsContext.h"

namespace ffmpeg
{

struct InputFile;
typedef std::vector<InputFile> InputFiles;
struct InputStream;
typedef std::vector<InputStream*> InputStreamPtrs;
struct OutputFile;
struct OutputStream;
struct FilterGraph;

struct Filter
{
   AVFilterContext *filter;
   FilterGraph &graph;
   std::string name;

   Filter(FilterGraph &f) : filter(NULL), graph(f) {}
   virtual void describe_filter_link(AVFilterInOut *inout) = 0;

 protected:
   void describe_filter_link(AVFilterInOut *inout, bool in);
};

struct InputFilter : public Filter
{
   InputStream &ist; // NULL if takes no input from a stream

   InputFilter(FilterGraph &f, InputStream &s);
   void describe_filter_link(AVFilterInOut *inout) { Filter::describe_filter_link(inout,true); }
};

typedef std::vector<InputFilter> InputFilters;
typedef std::vector<InputFilter*> InputFilterPtrs;

struct OutputFilter : public Filter
{
   OutputStream *ost; // NULL if does not output to a stream

   /* temporary storage until stream maps are processed */
   AVFilterInOut *out_tmp;
   AVMediaType type;

   OutputFilter(FilterGraph &f, OutputStream &s) : Filter(f), ost(&s), out_tmp(NULL), type(AVMEDIA_TYPE_UNKNOWN) {}

   OutputFilter(FilterGraph &f, AVFilterInOut *io, const AVMediaType t) : Filter(f), ost(NULL), out_tmp(io), type(t) {}

  void init_output_filter(OutputFile &ofile, OptionsContext &o);
  void describe_filter_link(AVFilterInOut *inout) { Filter::describe_filter_link(inout, false); }
};
typedef std::vector<OutputFilter> OutputFilters;
typedef std::vector<OutputFilter*> OutputFilterPtrs;

struct FilterGraph : public ffmpegBase
{

  // construct a complex filtergraph (just description)
  FilterGraph::FilterGraph(const int i, const std::string &desc);

   // construct simple filtergraph
   FilterGraph::FilterGraph(const int i, InputStream &ist, OutputStream &ost);
   ~FilterGraph();

   bool ist_in_filtergraph(InputStream &ist);
   bool filtergraph_is_simple();

   int configure_filtergraph();
   int init_complex_filtergraph(const InputFiles &input_files, InputStreamPtrs &input_streams);

   int index;
   std::string graph_desc;

   AVFilterGraph *graph;
   int reconfiguration;

   InputFilters inputs;
   OutputFilters outputs;

   int audio_sync_method;
   float audio_drift_threshold;

   bool do_deinterlace;

 private:
   void init_input_filter(AVFilterInOut *in, InputStreamPtrs &input_streams); // called from init_complex_filtergraph()

   int auto_insert_filter_input(InputStream &ist, const std::string &opt_name, const std::string &filter_name, const std::string &arg, AVFilterContext *&last_filter) const;
   int auto_insert_filter(const std::string &opt_name, const std::string &filter_name, const std::string &arg, int &pad_idx, AVFilterContext *&last_filter) const;

   int configure_input_filter(InputFilter &ifilter, AVFilterInOut *in);

   int configure_input_video_filter(InputFilter &ifilter, AVFilterInOut *in);
   int configure_input_audio_filter(InputFilter &ifilter, AVFilterInOut *in);

   int configure_output_filter(OutputFilter &ofilter, AVFilterInOut *out);
   int configure_output_video_filter(OutputFilter &ofilter, AVFilterInOut *out);
   int configure_output_audio_filter(OutputFilter &ofilter, AVFilterInOut *out);

   static int insert_trim(int64_t start_time, int64_t duration, AVFilterContext *&last_filter, int &pad_idx, const std::string &filter_name);
   static int insert_filter(AVFilterContext *&last_filter, int &pad_idx, const std::string &filter_name, const std::string &args);

   static std::string FilterGraph::options_to_string(const AVDictionary* dict);
};

typedef std::vector<FilterGraph> FilterGraphs;
typedef std::vector<FilterGraph&> FilterGraphRefs;

}
