#include "ffmpegOptionsContext.h"

#include <sstream> //std::ostringstream
#include <utility> // std::pair

extern "C" {
#include "libavutil/mem.h"
}

using namespace ffmpeg;

OptionsContext::OptionsContext(OptionDefs &all_defs, const int flags)
{
   // create the vector of OptionDefs from the main list of options. Only the options of which flags matches the given reference flags
   for (auto def = all_defs.begin(); def < all_defs.end(); def++)
   {
      if (def->flags & flags)
         defs.emplace_back(*def);
   }

   //init();
}

OptionsContext::~OptionsContext()
{
   // delete all options
   for (auto opt = opts.begin(); opt != opts.end(); opt++)
      delete *opt;
}

const Option *OptionsContext::cfind(const std::string &name) const // returns a pointer to the requested option or NULL if does not exist
{
   auto opt = cfind_option(name);
   if (cfind_option(name) != opts.end())
      return *opt;
   else
      return NULL;
}

// parse the option key-value string pairs from OptionParseContext object
void OptionsContext::parse(const OptionGroup &g)
// int parse_optgroup(void *optctx, OptionGroup *g)
{
   // parse all options of the group
   for (auto o = g.opts.begin(); o < g.opts.end(); o++)
      write_option(o->opt, o->key, o->val);

   // copy options in the dictionary
   if (av_dict_copy(&codec_opts, g.codec_opts, 0) < 0 || av_dict_copy(&format_opts, g.format_opts, 0) < 0 || av_dict_copy(&sws_dict, g.sws_dict, 0) < 0 || av_dict_copy(&swr_opts, g.swr_opts, 0) < 0)
      throw ffmpegException("Failed to copy AV Dictionaries.");
}

OptionDefRefs::iterator OptionsContext::find_optiondef(const std::string &name)
{
   for (auto def = defs.begin(); def != defs.end(); def++)
   {
      if (def->get().name == name)
         return def;
   }
   return defs.end();
}

OptionDefRefs::const_iterator OptionsContext::cfind_optiondef(const std::string &name) const
{
   for (auto def = defs.begin(); def != defs.end(); def++)
   {
      if (def->get().name == name)
         return def;
   }
   return defs.end();
}

Options::iterator OptionsContext::find_option(const std::string &name)
{
   for (auto opt = opts.begin(); opt != opts.end(); opt++)
   {
      if ((*opt)->def.name == name)
         return opt;
   }
   return opts.end();
}

Options::const_iterator OptionsContext::cfind_option(const std::string &name) const
{
   for (auto opt = opts.begin(); opt != opts.end(); opt++)
   {
      if ((*opt)->def.name == name)
         return opt;
   }
   return opts.end();
}

Options::iterator OptionsContext::find_option(const OptionDef &def)
{
   for (auto opt = opts.begin(); opt != opts.end(); opt++)
   {
      if (&((*opt)->def) == &(def))
         return opt;
   }
   return opts.end();
}

Options::const_iterator OptionsContext::cfind_option(const OptionDef &def) const
{
   for (auto opt = opts.begin(); opt != opts.end(); opt++)
   {
      if (&((*opt)->def) == &(def))
         return opt;
   }
   return opts.end();
}

Options::iterator OptionsContext::find_or_create_option(const OptionDef &def)
{
   // look for the option already defined in the context
   Options::iterator opt = find_option(def);

   // if option does not exist, create an entry in opts
   if (opt == opts.end())
   {
      std::pair<Options::iterator, bool> res;
      if (def.flags & OPT_SPEC)
      {
         if (def.flags & OPT_STRING)
            res = opts.insert(new SpecifierOptsString(def));
         else if ((def.flags & OPT_TYPE) == OPT_BOOL)
            res = opts.insert(new SpecifierOptsBool(def));
         else if ((def.flags & OPT_TYPE) == OPT_INT)
            res = opts.insert(new SpecifierOptsInt(def));
         else if ((def.flags & OPT_TYPE) == OPT_INT64)
            res = opts.insert(new SpecifierOptsInt64(def));
         else if ((def.flags & OPT_TYPE) == OPT_FLOAT)
            res = opts.insert(new SpecifierOptsFloat(def));
         else if ((def.flags & OPT_TYPE) == OPT_DOUBLE)
            res = opts.insert(new SpecifierOptsDouble(def));
         else if ((def.flags & OPT_TYPE) == OPT_TIME)
            res = opts.insert(new SpecifierOptsTime(def));
      }
      else
      {
         if (def.flags & OPT_STRING)
            res = opts.insert(new OptionString(def));
         else if ((def.flags & OPT_TYPE) == OPT_BOOL)
            res = opts.insert(new OptionBool(def));
         else if ((def.flags & OPT_TYPE) == OPT_INT)
            res = opts.insert(new OptionInt(def));
         else if ((def.flags & OPT_TYPE) == OPT_INT64)
            res = opts.insert(new OptionInt64(def));
         else if ((def.flags & OPT_TYPE) == OPT_FLOAT)
            res = opts.insert(new OptionFloat(def));
         else if ((def.flags & OPT_TYPE) == OPT_DOUBLE)
            res = opts.insert(new OptionDouble(def));
         else if ((def.flags & OPT_TYPE) == OPT_TIME)
            res = opts.insert(new OptionTime(def));
      }

      // check for successful insertion, throw exception if failed
      if (!res.second)
      {
         std::ostringstream msg;
         msg << "Could not insert Option " << def.name << ".";
         throw ffmpegException(msg.str());
      }

      // get the iterator pointing to the newly created option
      opt = res.first;
   }

   return opt;
}

Options::iterator OptionsContext::write_option(const OptionDef &def, const std::string &name, const std::string &arg)
{
   // get option entry
   Options::iterator opt = find_or_create_option(def);

   // set the value
   if (def.flags & OPT_SPEC)
      (*opt)->parse(name, arg); // pass the original option name:spec string
   else
      (*opt)->parse(arg);

   return opt;
}
