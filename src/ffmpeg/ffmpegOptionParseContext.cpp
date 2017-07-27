#include "ffmpegOptionParseContext.h"

#include "ffmpegException.h"

using namespace ffmpeg;

const OptionGroupDef OptionParseContext::global_group = {"global"}; // cmdutils.cpp

//static void init_parse_context(OptionParseContext *octx, const OptionGroupDef *groups, int nb_groups) // cmdutils.cpp
OptionParseContext::OptionParseContext(const std::vector<OptionGroupDef> &ogd) : group_defs(ogd)
{
   global_opts.def = &global_group;
}

//void uninit_parse_context(OptionParseContext *octx) // cmdutils.cpp
OptionParseContext::~OptionParseContext() {}

//int split_commandline(OptionParseContext *octx, int argc, char *argv[], const OptionDef *options, const OptionGroupDef *groups, int nb_groups) // cmdutils.c
// ffmpeg [global_options] {[input_file_options] -i input_url} ... {[output_file_options] output_url} ...
void OptionParseContext::split_commandline(int argc, char *argv[], const OptionDefs &options)
{
   std::ostringstream msg; // only used to throw exception
   int optindex = 1;
   int dashdash = -2;

   //av_log(NULL, AV_LOG_DEBUG, "Splitting the commandline.\n");

   // for each argument string
   while (optindex < argc)
   {
      std::string opt = argv[optindex++]; // argument
      const char *arg;
      int ret;

      //av_log(NULL, AV_LOG_DEBUG, "Reading option '%s' ...", opt);

      if (opt == "--") // double dash: "--" (undocumented??), force to end option grouping (e.g., 2 output files)
      {
         dashdash = optindex; // dashdash contains the option argument index
         continue;
      }

      /* unnamed group separators, e.g. output filename */
      if (!(opt.front() == '-' && opt.size() > 1) || dashdash + 1 == optindex) //
      {
         auto g = match_group_separator(opt);
         if (g == group_defs.end()) // unnamed group separators not supported
         {
            msg << "Unnamed group separator is not supported: '" << opt << "'";
            throw ffmpegException(msg.str());
         }

         // current group completed, wrap it up and add it to the list
         finish_group(*g, opt);
         continue;
      }

      // remove the dash
      opt.erase(0, 1);

#define GET_ARG(arg)                                            \
   {                                                            \
      arg = argv[optindex++];                                   \
      if (!arg)                                                 \
      {                                                         \
         msg << "Missing argument for option '" << opt << "'."; \
         throw ffmpegException(msg.str());                      \
      }                                                         \
   }

      /* named group separators, e.g. -i */
      auto g = match_group_separator(opt);
      if (g != group_defs.end()) ////
      {
         GET_ARG(arg)
         finish_group(*g, arg);
         //av_log(NULL, AV_LOG_DEBUG, " matched as %s with argument '%s'.\n", g->name, arg);
         continue;
      }

      /* normal options */
      auto po = find_option(opt, options);
      if (po != options.end())
      {
         // if alias option, get the actual option definition
         if (po->flags & IS_ALIAS)
            po = find_option(po->realname, options);

         if (po->flags & OPT_EXIT) /* optional argument, e.g. -h */
            arg = argv[optindex++];
         else if (po->flags & HAS_ARG)
            GET_ARG(arg)
         else // if argument not given, use "1" as default
            arg = "1";

         add_opt(*po, opt, arg);
         //av_log(NULL, AV_LOG_DEBUG, " matched as option '%s' (%s) with argument '%s'.\n", po->name, po->help, arg);
         continue;
      }

      /* AVOptions if there is an argument*/
      if (argv[optindex])
      {
         ret = groups.back().opt_default(opt, argv[optindex]);
         if (ret >= 0)
         {
            //av_log(NULL, AV_LOG_DEBUG, " matched as AVOption '%s' with argument '%s'.\n", opt, argv[optindex]);
            optindex++;
            continue;
         }
         else if (ret != AVERROR_OPTION_NOT_FOUND)
         {
            msg << "Error parsing option '" << opt << "' with argument '" << argv[optindex] << "'.";
            throw ffmpegException(msg.str());
         }
      }

      /* boolean -nofoo options */
      if (opt[0] == 'n' && opt[1] == 'o' && (po = find_option(opt.substr(2), options)) != options.end() && po->flags & OPT_BOOL)
      {
         add_opt(*po, opt, "0");
         //av_log(NULL, AV_LOG_DEBUG, " matched as option '%s' (%s) with argument 0.\n", po->name, po->help);
         continue;
      }

      msg << "Unrecognized option '"<< opt << "'.";
      throw ffmpegException(msg.str());
   }

   // if (groups.back().opts.size() || groups.back().codec_opts || groups.back().format_opts)
   //    av_log(NULL, AV_LOG_WARNING, "Trailing options were found on the commandline.\n");

   //av_log(NULL, AV_LOG_DEBUG, "Finished splitting the commandline.\n");
}

/*
 * Finish parsing an option group: moving current option 
 *
 * @param group_idx which group definition should this group belong to
 * @param arg argument of the group delimiting option
 */
//static void finish_group(OptionParseContext *octx, int group_idx, const char *arg)
void OptionParseContext::finish_group(const OptionGroupDef &d, const std::string &a)
{
   // finalize the group
   if (groups.size())
      groups.back().finalize(&d, a);

   // create new group entry in the current list
   groups.emplace_back();
}

/*
 * Add an option instance to currently parsed group.
 */
//static void add_opt(OptionParseContext *octx, const OptionDef *opt, const char *key, const char *val) // cmdutils.cpp
void OptionParseContext::add_opt(const OptionDef &opt, const std::string &key, const std::string &val)
{
   // if option is a global option, write to global_opts, otherwise write to the current option group
   OptionGroup &g = !(opt.flags & (OPT_GLOBAL)) ? global_opts : groups.back();
   g.opts.emplace_back(opt, key, val);
}

/*
 * Check whether given option is a group separator.
 *
 * @return index of the group definition that matched or -1 if none
 */
OptionGroupDefs::const_iterator OptionParseContext::match_group_separator(const std::string &opt)
{
   OptionGroupDefs::const_iterator g = group_defs.begin();
   for (; g < group_defs.end(); g++)
   {
      if (g->sep == opt)
         return g;
   }

   return g;
}

OptionDefs::const_iterator OptionParseContext::find_option(const std::string &opt, const OptionDefs &defs, OptionDefs::const_iterator po)
{
   // option may contain trailing stream/file specifier, delimited by ':'.
   std::string name = opt.substr(0, opt.find(':'));

   for (; po < defs.end(); po++)
   {
      if (po->name == name)
         break;
   }
   return po;
}
