#pragma once

#include <limits>
#include <string>
#include <vector>
#include <sstream>
#include <set>
#include <map>
#include <functional> // std::refernce_wrapper

extern "C" {
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include <libavutil/parseutils.h>
#include <libavutil/eval.h>
#include <libavformat/avformat.h>
}

#include "ffmpegBase.h"
#include "ffmpegPtrs.h"
#include "ffmpegOptionDefs.h"
#include "ffmpegException.h"

/*
 * CLASS/STRUCT/TYPDEF defined in this file
 *  struct OptionPair
 *  typedef std::vector<OptionPair> OptionPairs;
 *  struct AvOptionGroup : public ffmpegBase
 *  struct OptionGroup : AvOptionGroup // 3 types: global/input/output (for ffmpeg)
 *  typedef std::vector<OptionGroup> OptionGroups;
 *  typedef std::vector<OptionGroup &> OptionGroupRefs;
 *  struct Option
 *  struct OptionComp
 *  typedef std::set<Option *, OptionComp> Options;
 *  template <class T> struct OptionBase : public Option
 *  struct OptionBool : public OptionBase<bool> //#define OPT_BOOL 0x0002
 *  struct OptionString : public OptionBase<std::string> //#define OPT_STRING 0x0008
 *  struct OptionInt : public OptionBase<int> //#define OPT_INT 0x0080
 *  struct OptionFloat : public OptionBase<float> //#define OPT_FLOAT 0x0100
 *  struct OptionDouble : public OptionBase<double> //#define OPT_DOUBLE 0x20000
 *  struct OptionInt64 : public OptionBase<int64_t> //#define OPT_INT64 0x0400
 *  template <class T> struct SpecifierOpts : public OptionBase<std::map<std::string, T>> //#define OPT_SPEC 0x8000 option is to be stored in an array of SpecifierOpt.
 *  struct SpecifierOptsBool : public SpecifierOpts<bool> //#define OPT_BOOL 0x0002
 *  struct SpecifierOptsString : public SpecifierOpts<std::string> //#define OPT_STRING 0x0008
 *  struct SpecifierOptsInt : public SpecifierOpts<int> //#define OPT_INT 0x0080
 *  struct SpecifierOptsFloat : public SpecifierOpts<float> //#define OPT_FLOAT 0x0100
 *  struct SpecifierOptsDouble : public SpecifierOpts<double> //#define OPT_DOUBLE 0x20000
 *  struct SpecifierOptsInt64 : public SpecifierOpts<int64_t> //#define OPT_INT64 0x0400
 *  struct OptionTime : public OptionBase<int64_t> //#define OPT_TIME 0x10000
 *  struct SpecifierOptsTime : public SpecifierOpts<int64_t> //#define OPT_DOUBLE 0x20000
 */

namespace ffmpeg
{

/**
 * An option key-value pair extracted from the commandline.
 * Cannot use AVDictionary because of options like -map which can be
 * used multiple times.
 */
struct OptionPair
{
   const OptionDef &opt;
   const std::string key;
   const std::string val;
   OptionPair(const OptionDef &o, const std::string &k, const std::string &v) : opt(o), key(k), val(v) {}
};
typedef std::vector<OptionPair> OptionPairs;

////////////////////////////////////////////////////////////

struct AvOptionGroup : public ffmpegBase
{
   AVDictionary *codec_opts;
   AVDictionary *format_opts;
   AVDictionary *sws_dict;
   AVDictionary *swr_opts;

   AvOptionGroup();
   ~AvOptionGroup();

   // try to set options directly to the AVDictionary object
   int opt_default(const std::string &opt, const std::string &arg);

   const AVOption *opt_find(void *obj, const std::string &name, const std::string &unit, int opt_flags, int search_flags) const;

  /**
 * Read packets of a media file to get stream information. This
 * is useful for file formats with no headers such as MPEG. This
 * function also computes the real framerate in case of MPEG-2 repeat
 * frame mode.
 * The logical file position is not changed by this function;
 * examined packets may be buffered for later processing.
 *
 * @param ic media file handle
 * @return vector of stream-wise dictionary, which are filled with options that were not found.
 *
 * @note this function isn't guaranteed to open all the codecs, so
 *       options being non-empty at return is a perfectly normal behavior.
 *
 * @todo Let the user decide somehow what information is needed so that
 *       we do not waste time getting stuff the user does not need.
 */
 std::vector<DictPtr> find_stream_info(AVFormatContext *ic);

  /**
 * Filter out options for given codec.
 *
 * Create a new options dictionary containing only the options from
 * opts which apply to the codec with ID codec_id.
 *
 * @param opts     dictionary to place options in
 * @param codec_id ID of the codec that should be filtered for
 * @param s Corresponding format context.
 * @param st A stream from s for which the options should be filtered.
 * @param codec The particular codec for which the options should be filtered.
 *              If null, the default one is looked up according to the codec id.
 * @return a pointer to the created dictionary
 */
   AVDictionary *filter_codec_opts(AVCodecID codec_id, AVFormatContext *s, AVStream *st, AVCodec *codec) const;

private:
   /**
 * Setup AVCodecContext options for avformat_find_stream_info().
 *
 * Create an array of dictionaries, one dictionary for each stream
 * contained in s.
 * Each dictionary will contain the options from codec_opts which can
 * be applied to the corresponding stream codec context.
 *
 * @return std::vector of dictionaries, empty if it
 * cannot be created
 */
   std::vector<DictPtr> setup_find_stream_info_opts(AVFormatContext *s);

};

//////////////////////////////////

struct OptionGroup : AvOptionGroup // 3 types: global/input/output (for ffmpeg)
{
   bool valid;                //
   const OptionGroupDef *def; // not reference as the group type may not be known at the time of instaitation
   std::string arg;           // main argument (e.g., file name)

   OptionPairs opts; // vector of option key-value pairs

   OptionGroup() : valid(false), def(NULL) {}
   ~OptionGroup() {}

   void finalize(const OptionGroupDef *d, const std::string &a);
};

typedef std::vector<OptionGroup> OptionGroups;
typedef std::vector<OptionGroup &> OptionGroupRefs;

//////////////////////////////////

struct Option
{
   const OptionDef &def;

   Option(const OptionDef &d) : def(d) // value uninitialized
   {
      validate();
   }

   // option definition member access function
   const std::string &name() const { return def.name; }
   const int &flags() const { return def.flags; }
   const std::string &help() const { return def.help; }
   const std::string &argname() const { return def.argname; }

   // pure virtual functions
   virtual void validate() const = 0; // check if def.flags is compatible with the class
   virtual void parse(const std::string &str) = 0;
   virtual void parse(const std::string &opt, const std::string &arg) { throw ffmpegException("This option class does not define 2-argument parse() function."); };

   double parse_number(const std::string str);
};

struct OptionComp
{
   bool operator()(const Option *lhs, const Option *rhs) const { return lhs->name() < rhs->name(); }
};

typedef std::set<Option *, OptionComp> Options;

template <class T>
struct OptionBase : public Option
{
   T value;

   OptionBase(const OptionDef &d) : Option(d) // value uninitialized
   {
      validate();
   }
   OptionBase(const OptionDef &d, const T &v) : Option(d), value(v) // value initialized
   {
      validate();
   }

   // pure virtual functions from OptionBase, none implemented
   // virtual void validate() const= 0; // check if def.flags is compatible with the class
   // virtual void parse(const std::string &str) = 0;

   virtual void set(const T &v) { value = v; }
   virtual const T &get() const { return value; }
};

// create specialization
struct OptionBool : public OptionBase<bool> //#define OPT_BOOL 0x0002
{
   OptionBool(const OptionDef &d) : OptionBase(d) {}
   OptionBool(const OptionDef &d, const bool &v) : OptionBase(d, v) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_BOOL))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &str)
   {
      value = (bool)parse_number(str);
   }
};

struct OptionString : public OptionBase<std::string> //#define OPT_STRING 0x0008
{
   OptionString(const OptionDef &d) : OptionBase(d) {}
   OptionString(const OptionDef &d, const std::string &v) : OptionBase(d, v) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_STRING))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &str)
   {
      value = str;
   }
};

struct OptionInt : public OptionBase<int> //#define OPT_INT 0x0080
{
   OptionInt(const OptionDef &d) : OptionBase(d) {}
   OptionInt(const OptionDef &d, const int &v) : OptionBase(d, v) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_INT))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &str)
   {
      double num = parse_number(str);
      value = (int)num;
      if (num != (double)value)
      {
         std::ostringstream msg;
         msg;
         msg << "Expected int for " << name() << " but found " << str;
         throw ffmpegException(msg.str());
      }
   }
};

struct OptionFloat : public OptionBase<float> //#define OPT_FLOAT 0x0100
{
   OptionFloat(const OptionDef &d) : OptionBase(d) {}
   OptionFloat(const OptionDef &d, const float &v) : OptionBase(d, v) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_FLOAT))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &str)
   {
      value = (float)parse_number(str);
   }
};

struct OptionDouble : public OptionBase<double> //#define OPT_DOUBLE 0x20000
{
   OptionDouble(const OptionDef &d) : OptionBase(d) {}
   OptionDouble(const OptionDef &d, const double &v) : OptionBase(d, v) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_DOUBLE))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &str)
   {
      value = parse_number(str);
   }
};

struct OptionInt64 : public OptionBase<int64_t> //#define OPT_INT64 0x0400
{
   OptionInt64(const OptionDef &d) : OptionBase(d) {}
   OptionInt64(const OptionDef &d, const int64_t &v) : OptionBase(d, v) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_INT64))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &str)
   {
      double num = parse_number(str);
      value = (int64_t)num;
      if (num != (double)value)
      {
         std::ostringstream msg;
         msg << "Expected int64 for " << name() << " but found " << str;
         throw ffmpegException(msg.str());
      }
   }
};

// options with stream specifier
template <class T>
struct SpecifierOpts : public OptionBase<std::map<std::string, T>> //#define OPT_SPEC 0x8000 /* option is to be stored in an array of SpecifierOpt. */
{
   typename std::map<std::string, T>::iterator last; // 

   SpecifierOpts(const OptionDef &d) : OptionBase(d), last(value.begin()) {}
   virtual void set(const std::string &spec, const T &val) // option argument
   {
      auto ret = value.insert_or_assign(spec, val);
      last = ret.first;
   }

   void parse(const std::string &str)
   {
      throw ffmpegException("SpecifierOpts requires both option and its argument strings. Use parse(opt,arg).");
   }
   virtual void parse(const std::string &opt, const std::string &arg) = 0;

   // per-type get
   virtual const T &get(const std::string &mediatype) const
   {
      auto entry = value.find(mediatype);
      if (entry == value.end())
         throw ffmpegException("Option not found for the specified media type.");
      return entry->second;
   }

   virtual const T &get(const AVMediaType type) const
   {
      switch (type)
      {
      case AVMEDIA_TYPE_VIDEO:
         return get("v");
      case AVMEDIA_TYPE_AUDIO:
         return get("a");
      case AVMEDIA_TYPE_SUBTITLE:
         return get("s");
      case AVMEDIA_TYPE_DATA:
         return get("d");
      case AVMEDIA_TYPE_ATTACHMENT:
         return get("t");
      }
      throw ffmpegException("Option not found for the specified media type");
   }

   // per-stream get
   virtual const T &get(AVFormatContext *s, AVStream *st) const
   {
      const T *generic = NULL;

      for (auto entry = value.begin(); entry != value.end(); entry++)
      {
         // get the first match
         //>0 if st is matched by spec; 0 if st is not matched by spec; AVERROR code if spec is invalid
         if (avformat_match_stream_specifier(s, st, entry->first.c_str()) > 0) // match found
         {
            if (entry->first.empty()) // if generic, save and continue searching for a specific
               generic = &entry->second;
            else
               return entry->second;
         }
      }

      // if generic (no specifier) was found, return it
      if (generic)
         return *generic;

      // error out if no matching entry entry
      throw ffmpegException("Option not found for the specified stream.");
   }
};

// create specialization
struct SpecifierOptsBool : public SpecifierOpts<bool> //#define OPT_BOOL 0x0002
{
   SpecifierOptsBool(const OptionDef &d) : SpecifierOpts(d) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_BOOL | OPT_SPEC))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &opt, const std::string &arg)
   {
      set(opt, (bool)parse_number(arg));
   }
};

struct SpecifierOptsString : public SpecifierOpts<std::string> //#define OPT_STRING 0x0008
{
   SpecifierOptsString(const OptionDef &d) : SpecifierOpts(d) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_STRING | OPT_SPEC))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &opt, const std::string &arg)
   {
      set(opt, arg);
   }
};

struct SpecifierOptsInt : public SpecifierOpts<int> //#define OPT_INT 0x0080
{
   SpecifierOptsInt(const OptionDef &d) : SpecifierOpts(d) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_INT | OPT_SPEC))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &opt, const std::string &arg)
   {
      double num = parse_number(arg);
      int val = (int)num;
      if (num != (double)val)
      {
         std::ostringstream msg;
         msg << "Expected int for " << opt << " but found " << arg;
         throw ffmpegException(msg.str());
      }
      set(opt, val);
   }
};

struct SpecifierOptsFloat : public SpecifierOpts<float> //#define OPT_FLOAT 0x0100
{
   SpecifierOptsFloat(const OptionDef &d) : SpecifierOpts(d) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_FLOAT | OPT_SPEC))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &opt, const std::string &arg)
   {
      set(opt, (float)parse_number(arg));
   }
};

struct SpecifierOptsDouble : public SpecifierOpts<double> //#define OPT_DOUBLE 0x20000
{
   SpecifierOptsDouble(const OptionDef &d) : SpecifierOpts(d) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_DOUBLE | OPT_SPEC))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &opt, const std::string &arg)
   {
      set(opt, parse_number(arg));
   }
};

struct SpecifierOptsInt64 : public SpecifierOpts<int64_t> //#define OPT_INT64 0x0400
{
   SpecifierOptsInt64(const OptionDef &d) : SpecifierOpts(d) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_INT64 | OPT_SPEC))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &opt, const std::string &arg)
   {
      double num = parse_number(arg);
      int64_t val = (int64_t)num;
      if (num != (double)val)
      {
         std::ostringstream msg;
         msg << "Expected int64 for " << opt << " but found " << arg;
         throw ffmpegException(msg.str());
      }
      set(opt, val);
   }
};

struct OptionTime : public OptionBase<int64_t> //#define OPT_TIME 0x10000
{
   OptionTime(const OptionDef &d) : OptionBase(d) {}
   OptionTime(const OptionDef &d, const uint64_t &v) : OptionBase(d, v) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_TIME))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &str)
   {
      if (av_parse_time(&value, str.c_str(), true) < 0)
      {
         std::ostringstream msg;
         msg << "Invalid duration specification:" << str;
         throw ffmpegException(msg.str());
      }
   }
};

struct SpecifierOptsTime : public SpecifierOpts<int64_t> //#define OPT_DOUBLE 0x20000
{
   SpecifierOptsTime(const OptionDef &d) : SpecifierOpts(d) {}
   void validate() const // check if def.flags is compatible with the class
   {
      if (!(def.flags & OPT_TIME | OPT_SPEC))
         throw ffmpegException("Incompatible option class and associated option definition.");
   }
   virtual void parse(const std::string &opt, const std::string &arg)
   {
      int64_t val;
      if (av_parse_time(&val, arg.c_str(), true) < 0)
      {
         std::ostringstream msg;
         msg << "Invalid duration specification:" << arg;
         throw ffmpegException(msg.str());
      }
      set(opt, val);
   }
};
}
