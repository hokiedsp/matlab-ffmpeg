#include "ffmpegStreamMap.h"

#include <sstream>

#include "ffmpegException.h"

using namespace ffmpeg;

// [-]input_file_id[:stream_specifier][?][,sync_file_id[:stream_specifier]] | [linklabel]
static int opt_map(std::string arg, InputFiles &input_files)
{
   std::ostringstream msg;

   // OptionsContext *o = optctx;
   // StreamMap *m = NULL;
   int i, negative = 0, file_idx;
   int sync_file_idx = -1, sync_stream_idx = 0;
   char *p, *sync;
   char *map = arg.data();
   char *allow_unused;

   if (*map == '-')
   {
      negative = 1;
      map++;
   }
   
   if (!map)
      return AVERROR(ENOMEM);

   /* parse sync stream first, just pick first matching stream */
   if (sync = strchr(map, ','))
   {
      *sync = 0;
      sync_file_idx = strtol(sync + 1, &sync, 0);

      if (sync_file_idx >= input_files.size() || sync_file_idx < 0)
      {
         msg << "Invalid sync file index: " << sync_file_idx << ".";
         throw ffmpegException(msg.str());
      }
      if (*sync)
         sync++;

      InputFile &syncfile = input_files[sync_file_idx];

      for (auto ist = syncfile.streams.begin(); ist < syncfile.streams.end(); ist++)
      {
         // int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec)
         // {
         //    int ret = avformat_match_stream_specifier(s, st, spec);
         //    if (ret < 0)
         //       av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
         //    return ret;
         // }
         if (check_stream_specifier(syncfile->ctx, ist->st, sync) == 1)
         {
            sync_stream_idx = i;
            break;
         }
      }
      if (i == syncfile.streams.size())
         throw ffmpegException("Sync stream specification in map " + arg + " does not match any streams.");
   }

   // if starts with bracket
   if (map[0] == '[')
   {
      /* this mapping refers to lavfi output */
      const char *c = map + 1;
      GROW_ARRAY(o.stream_maps, o.nb_stream_maps);
      m = &o.stream_maps[o.nb_stream_maps - 1];
      m->linklabel = av_get_token(&c, "]");
      if (!m->linklabel)
         throw ffmpegException("Invalid output link label: " + map + ".");
   }
   else
   {
      if (allow_unused = strchr(map, '?'))
         *allow_unused = 0;
      file_idx = strtol(map, &p, 0);
      if (file_idx >= nb_input_files || file_idx < 0)
      {
         msg << "Invalid input file index: " << file_idx << ".");
         throw ffmpegException(msg.str());
      }
      if (negative)
         /* disable some already defined maps */
         for (i = 0; i < o.nb_stream_maps; i++)
         {
            m = &o.stream_maps[i];
            if (file_idx == m->file_index && check_stream_specifier(input_files[m->file_index]->ctx, input_files[m->file_index]->ctx->streams[m->stream_index], *p == ':' ? p + 1 : p) > 0)
               m->disabled = 1;
         }
      else
         for (i = 0; i < input_files[file_idx]->nb_streams; i++)
         {
            if (check_stream_specifier(input_files[file_idx]->ctx, input_files[file_idx]->ctx->streams[i], *p == ':' ? p + 1 : p) <= 0)
               continue;
            GROW_ARRAY(o.stream_maps, o.nb_stream_maps);
            m = &o.stream_maps[o.nb_stream_maps - 1];

            m->file_index = file_idx;
            m->stream_index = i;

            if (sync_file_idx >= 0)
            {
               m->sync_file_index = sync_file_idx;
               m->sync_stream_index = sync_stream_idx;
            }
            else
            {
               m->sync_file_index = file_idx;
               m->sync_stream_index = i;
            }
         }
   }

   if (!m)
   {
      if (allow_unused)
      {
         av_log(NULL, AV_LOG_VERBOSE, "Stream map '%s' matches no streams; ignoring.\n", arg);
      }
      else
         throw ffmpegException("Stream map '" + arg + "' matches no streams.");
   }

   return 0;
}
