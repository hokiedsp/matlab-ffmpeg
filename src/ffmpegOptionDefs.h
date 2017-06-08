#pragma once

#include <string>
#include <vector>

namespace ffmpeg
{
struct OptionDef
{
   std::string name;
   int flags;
#define OPT_TYPE 0x000F     // mask for data type
#define OPT_BOOL 0x0001     //   bool
#define OPT_STRING 0x0002   //   string
#define OPT_INT 0x0003      //   int
#define OPT_FLOAT 0x0004    //   float
#define OPT_INT64 0x0005    //   int64_t
#define OPT_TIME 0x0006     //   int64_t, time in us
#define OPT_DOUBLE 0x0007   //   double
#define OPT_MEDIA 0x0070    // mask for target media stream
#define OPT_VIDEO 0x0010    //   for video stream
#define OPT_AUDIO 0x0020    //   for audio stream
#define OPT_SUBTITLE 0x0030 //   for subtitle stream
#define OPT_DATA 0x0040     //   for data stream
#define OPT_SCOPE 0x0180    // mask for option scope
#define OPT_GLOBAL 0x0080   //   global option
#define OPT_INPUT 0x0100    //   input option
#define OPT_OUTPUT 0x0180   //   output option
#define IS_ALIAS 0x0200     // alias name
#define HAS_ARG 0x0400      // expects next input argument to be the option value
#define OPT_EXIT 0x0800     // ignores all subsequent arguments
#define OPT_SPEC 0x1000     // option is to be stored in an array of SpecifierOpt.
#define OPT_PERFILE 0x2000  // the option is per-file
   std::string help;        // help text -or- actual name if alias
   std::string argname;

   OptionDef(const std::string &n, const int &f, const std::string &h = "", const std::string &an = "") : name(n), flags(f), help(h), argname(an) {}
};
typedef std::vector<OptionDef> OptionDefs;
typedef std::vector<std::reference_wrapper<OptionDef>> OptionDefRefs;

////////////////////////////////////////////////////////////

// static const OptionGroupDef groups[] = {
//     [GROUP_OUTFILE] = { "output url",  NULL, OPT_OUTPUT },
//     [GROUP_INFILE]  = { "input url",   "i",  OPT_INPUT },
// };
struct OptionGroupDef // 3 instances in ffmpeg: global/input/output option groups
{
   /**< group name */
   const std::string name;
   /**
     * Option to be used as group separator. Can be NULL for groups which
     * are terminated by a non-option argument (e.g. ffmpeg output files)
     */
   const std::string sep;
   /**
     * Option flags that must be set on each option that is
     * applied to this group
     */
   int flags;
};
typedef std::vector<OptionGroupDef> OptionGroupDefs;

//////////////////////////////////

// functions to add options
OptionDefs & add_io_options(OptionDefs &defs); // append option definitions that are common to both input/output files
OptionDefs & add_in_options(OptionDefs &defs); // append option definitions that are unique to input files
OptionDefs & add_out_options(OptionDefs &defs); // append option definitions that are unique to output files
OptionDefs & add_filter_options(OptionDefs &defs); // append definitions for filtering options
}