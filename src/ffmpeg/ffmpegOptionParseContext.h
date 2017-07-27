#pragma once

#include <string>
#include <vector>

#include "ffmpegOption.h" // OptionGroup

namespace ffmpeg
{

struct OptionParseContext
{
   const OptionGroupDefs &group_defs;

   OptionGroup global_opts;

   OptionGroups groups; // lists of groups of option (option groups with same def are grouped)

   OptionParseContext(const OptionGroupDefs &ogd);
   ~OptionParseContext();
   void split_commandline(int argc, char *argv[], const OptionDefs &options);

 private:
   static const OptionGroupDef global_group;

   // find option in current goup
   //static const OptionDef *find_option(const OptionDef *po, const char *name)
   static OptionDefs::const_iterator find_option(const std::string &opt, const OptionDefs &defs, OptionDefs::const_iterator po);
   static OptionDefs::const_iterator find_option(const std::string &opt, const OptionDefs &defs) { return find_option(opt, defs, defs.begin()); };

   void finish_group(const OptionGroupDef &def, const std::string &arg);
   void add_opt(const OptionDef &opt, const std::string &key, const std::string &val);

   OptionGroupDefs::const_iterator match_group_separator(const std::string &opt);
};
}
