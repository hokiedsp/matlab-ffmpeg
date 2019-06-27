#include "ffmpegUtil.h"

#include "ffmpegException.h"

using namespace ffmpeg;

AVCodec *find_encoder(const std::string &name, const AVMediaType type)
{
   const AVCodecDescriptor *desc;
   AVCodec *codec;

   if (!(codec = avcodec_find_encoder_by_name(name.c_str())) && (desc = avcodec_descriptor_get_by_name(name.c_str())))
      codec = avcodec_find_encoder(desc->id);

   if (!codec)
      throw Exception("Unknown encoder " + name + "'");

   if (codec->type != type)
      throw Exception("Invalid encoder type '" + name + "'");

   return codec;
}

AVCodec *find_decoder(const std::string &name, const AVMediaType type)
{
   const AVCodecDescriptor *desc;
   AVCodec *codec;

   if (!(codec = avcodec_find_decoder_by_name(name.c_str())) && (desc = avcodec_descriptor_get_by_name(name)))
      codec = avcodec_find_decoder(desc->id);

   if (!codec)
      throw Exception("Unknown decoder " + name + "'");

   if (codec->type != type)
      throw Exception("Invalid decoder type '" + name + "'");

   return codec;
}

// remove entries from Dictionary A, which matches entries in Dictionary B
void remove_avoptions(AVDictionary *&a, AVDictionary *b)
{
   while ((AVDictionaryEntry *t = av_dict_get(b, "", t, AV_DICT_IGNORE_SUFFIX))) // for each entry in b
   {
      av_dict_set(&a, t->key, NULL, AV_DICT_MATCH_CASE); // if matching entry found, remove it
   }
}

// check to make sure dictionary is consumed???
void assert_avoptions(AVDictionary *m)
{
   if ((AVDictionaryEntry *t = av_dict_get(m, "", NULL, AV_DICT_IGNORE_SUFFIX)))
      throw Exception("Option " + t->key + "  not found.");
}

DictPtr strip_specifiers(AVDictionary *dict)
{
   AVDictionary *ret = NULL;
   AVDictionaryEntry *e = NULL;
   while ((e = av_dict_get(dict, "", e, AV_DICT_IGNORE_SUFFIX)))
   {
      char *p = strchr(e->key, ':');

      if (p)
         *p = 0;
      av_dict_set(&ret, e->key, e->value, 0);
      if (p)
         *p = ':';
   }
   return DictPtr(ret, delete_dict);
}

// set duration to max(tmp, duration) in a proper time base and return duration's time_base
AVRational duration_max(int64_t tmp, int64_t &duration, AVRational tmp_time_base, AVRational time_base)
{
   int ret;

   if (!*duration)
   {
      *duration = tmp;
      return tmp_time_base;
   }

   ret = av_compare_ts(*duration, time_base, tmp, tmp_time_base);
   if (ret < 0)
   {
      *duration = tmp;
      return tmp_time_base;
   }

   return time_base;
}

// void dump_attachment(AVStream *st, const std::string &filename)
// {
//    int ret;
//    AVIOContext *out = NULL;
//    AVDictionaryEntry *e;

//    if (!st->codecpar->extradata_size)
//    {
//       av_log(NULL, AV_LOG_WARNING, "No extradata to dump in stream #%d:%d.\n",
//              nb_input_files - 1, st->index);
//       return;
//    }
//    if (filename.empty() && (e = av_dict_get(st->metadata, "filename", NULL, 0)))
//       filename = e->value;
//    if (!*filename)
//    {
//       std::ostringstream msg;
//       msg << "No filename specified and no 'filename' tag in stream #" << st->index <<".";
//       throw Exception(msg.str());
//    }

//    assert_file_overwrite(filename);

//    if ((ret = avio_open2(&out, filename.c_str(), AVIO_FLAG_WRITE, &int_cb, NULL)) < 0)
//       throw Exception("Could not open file for writing.\n");

//    avio_write(out, st->codecpar->extradata, st->codecpar->extradata_size);
//    avio_flush(out);
//    avio_close(out);
// }

// void assert_file_overwrite(const std::string &filename)
// {
//    if (file_overwrite && no_file_overwrite)
//       throw Exception("Error, both -y and -n supplied. Exiting.");

//    if (!file_overwrite)
//    {
//       const char *proto_name = avio_find_protocol_name(filename.c_str());
//       if (proto_name && !strcmp(proto_name, "file") && avio_check(filename.c_str(), 0) == 0)
//       {
//          if (stdin_interaction && !no_file_overwrite)
//          {
//             fprintf(stderr, "File '%s' already exists. Overwrite ? [y/N] ", filename);
//             fflush(stderr);
//             term_exit();
//             signal(SIGINT, SIG_DFL);
//             if (!read_yesno())
//             {
//                av_log(NULL, AV_LOG_FATAL, "Not overwriting - exiting\n");
//                exit_program(1);
//             }
//             term_init();
//          }
//          else
//          {
//             av_log(NULL, AV_LOG_FATAL, "File '%s' already exists. Exiting.\n", filename);
//             exit_program(1);
//          }
//       }
//    }
// }
