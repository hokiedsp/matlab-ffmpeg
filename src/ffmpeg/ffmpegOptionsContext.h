#pragma once

#include <string>

extern "C" {
#include "libavutil/dict.h"
}

#include "ffmpegOption.h"

namespace ffmpeg
{

// each file gets an OptionsContext object
struct OptionsContext : public AvOptionGroup
{
   OptionDefRefs defs; // std::vector of expected options for the context
   Options opts;           // std::set of Option objects

   OptionsContext(OptionDefs &all_defs, const int flags); // given all the Options for the application. Keep only the options with matching flags
   ~OptionsContext();

   const Option *cfind(const std::string &opt) const; // returns a pointer to the requested option or NULL if does not exist

   template <class OPTTYPE, class VALTYPE>
   const VALTYPE *get(const std::string &name) const
   {
      const Option *opt = cfind(name);

      if (opt)
         return &((OPTTYPE *)opt)->value;
      else
         return NULL;
   }

   template <class OPTTYPE, class VALTYPE>
   void set(const std::string &name, const VALTYPE &val)
   {
      OptionDefRefs::iterator def = find_optiondef(name);
      if (def==defs.end())
         throw ffmpegException("Invalid option name: " + name);

      Options::iterator opt = find_or_create_option(*def);
      ((OPTTYPE &)*opt).value = val;
   }

   template <class SPECOPTTYPE, class VALTYPE>
   const VALTYPE *gettype(const std::string &name, const std::string &mediatype) const // returns a pointer to the requested option or NULL if does not exist
   {
      const Option *opt = cfind(name);

      if (opt)
      {
         try
         {
            return &((SPECOPTTYPE *)opt)->get(mediatype);
         }
         catch (...)
         {
            return NULL;
         }
      }
      else
         return NULL;
   }

   template <class SPECOPTTYPE, class VALTYPE>
   const VALTYPE *getspec(const std::string &name, AVFormatContext *s, AVStream *st) const // returns a pointer to the requested option or NULL if does not exist
   {
      const Option *opt = cfind(name);

      if (opt)
      {
         try
         {
            return &((SPECOPTTYPE *)opt)->get(s, st);
         }
         catch (...)
         {
            return NULL;
         }
      }
      else
         return NULL;
   }

   // populate options from OptionParseContext's group ()
   virtual void parse(const OptionGroup &g);

   // virtual void init() = 0; // to specify the initial option values by derived class
   // virtual void uninit() = 0; // to uninitialize the option values

 protected:
   OptionDefRefs::iterator find_optiondef(const std::string &name);
   Options::iterator find_option(const std::string &name);
   Options::iterator find_option(const OptionDef &def);
   OptionDefRefs::const_iterator cfind_optiondef(const std::string &name) const;
   Options::const_iterator cfind_option(const std::string &name) const;
   Options::const_iterator cfind_option(const OptionDef &def) const;
   Options::iterator find_or_create_option(const OptionDef &po);
   virtual Options::iterator write_option(const OptionDef &po, const std::string &opt, const std::string &arg);

};

}
