/*
 * ffmpeg option parsing
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "ffmpeg.h"
#include "cmdutils.h"

#include "libavformat/avformat.h"

#include "libavcodec/avcodec.h"

#include "libavfilter/avfilter.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/fifo.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

#define DEFAULT_PASS_LOGFILENAME_PREFIX "ffmpeg2pass"

#define MATCH_PER_STREAM_OPT(name, type, outvar, fmtctx, st)       \
   {                                                               \
      int i, ret;                                                  \
      for (i = 0; i < o.nb_##name; i++)                            \
      {                                                            \
         char *spec = o.name[i].specifier;                         \
         if ((ret = check_stream_specifier(fmtctx, st, spec)) > 0) \
            outvar = o.name[i].u.type;                             \
         else if (ret < 0)                                         \
            exit_program(1);                                       \
      }                                                            \
   }

const HWAccel hwaccels[] = {
#if HAVE_VDPAU_X11
    {"vdpau", vdpau_init, HWACCEL_VDPAU, AV_PIX_FMT_VDPAU},
#endif
#if HAVE_DXVA2_LIB
    {"dxva2", dxva2_init, HWACCEL_DXVA2, AV_PIX_FMT_DXVA2_VLD},
#endif
#if CONFIG_VDA
    {"vda", videotoolbox_init, HWACCEL_VDA, AV_PIX_FMT_VDA},
#endif
#if CONFIG_VIDEOTOOLBOX
    {"videotoolbox", videotoolbox_init, HWACCEL_VIDEOTOOLBOX, AV_PIX_FMT_VIDEOTOOLBOX},
#endif
#if CONFIG_LIBMFX
    {"qsv", qsv_init, HWACCEL_QSV, AV_PIX_FMT_QSV},
#endif
#if CONFIG_VAAPI
    {"vaapi", vaapi_decode_init, HWACCEL_VAAPI, AV_PIX_FMT_VAAPI},
#endif
#if CONFIG_CUVID
    {"cuvid", cuvid_init, HWACCEL_CUVID, AV_PIX_FMT_CUDA},
#endif
    {0},
};
int hwaccel_lax_profile_check = 0;

char *vstats_filename;
char *sdp_filename;

float dts_delta_threshold = 10;
float dts_error_threshold = 3600 * 30;

int video_sync_method = VSYNC_AUTO;
float frame_drop_threshold = 0;
int do_deinterlace = 0;
int do_benchmark = 0;
int do_benchmark_all = 0;
int do_hex_dump = 0;
int do_pkt_dump = 0;

// used in open_input_file/ffmpegInputFile
int start_at_zero = 0;

int copy_tb = -1;
int debug_ts = 0;
int abort_on_flags = 0;
int print_stats = -1;
int qp_hist = 0;
int stdin_interaction = 1;
int frame_bits_per_raw_sample = 0;
float max_error_rate = 2.0 / 3;

static int intra_only = 0;
static int file_overwrite = 0;
static int no_file_overwrite = 0;
static int do_psnr = 0;
static int input_sync;
static int override_ffserver = 0;
static int ignore_unknown_streams = 0;
static int copy_unknown_streams = 0;

static int show_hwaccels(void *optctx, const char *opt, const char *arg)
{
   int i;

   printf("Hardware acceleration methods:\n");
   for (i = 0; i < FF_ARRAY_ELEMS(hwaccels) - 1; i++)
   {
      printf("%s\n", hwaccels[i].name);
   }
   printf("\n");
   return 0;
}

static int opt_abort_on(void *optctx, const char *opt, const char *arg)
{
   static const AVOption opts[] = {
       {"abort_on", NULL, 0, AV_OPT_TYPE_FLAGS, {.i64 = 0}, INT64_MIN, INT64_MAX, .unit = "flags"},
       {"empty_output", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = ABORT_ON_FLAG_EMPTY_OUTPUT}, .unit = "flags"},
       {NULL},
   };
   static const AVClass class = {
       .class_name = "",
       .item_name = av_default_item_name,
       .option = opts,
       .version = LIBAVUTIL_VERSION_INT,
   };
   const AVClass *pclass = &class;

   return av_opt_eval_flags(&pclass, &opts[0], arg, &abort_on_flags);
}

static int opt_map(void *optctx, const char *opt, const char *arg)
{
   OptionsContext *o = optctx;
   StreamMap *m = NULL;
   int i, negative = 0, file_idx;
   int sync_file_idx = -1, sync_stream_idx = 0;
   char *p, *sync;
   char *map;
   char *allow_unused;

   if (*arg == '-')
   {
      negative = 1;
      arg++;
   }
   map = av_strdup(arg);
   if (!map)
      return AVERROR(ENOMEM);

   /* parse sync stream first, just pick first matching stream */
   if (sync = strchr(map, ','))
   {
      *sync = 0;
      sync_file_idx = strtol(sync + 1, &sync, 0);
      if (sync_file_idx >= nb_input_files || sync_file_idx < 0)
      {
         av_log(NULL, AV_LOG_FATAL, "Invalid sync file index: %d.\n", sync_file_idx);
         exit_program(1);
      }
      if (*sync)
         sync++;
      for (i = 0; i < input_files[sync_file_idx]->nb_streams; i++)
         if (check_stream_specifier(input_files[sync_file_idx]->ctx,
                                    input_files[sync_file_idx]->ctx->streams[i], sync) == 1)
         {
            sync_stream_idx = i;
            break;
         }
      if (i == input_files[sync_file_idx]->nb_streams)
      {
         av_log(NULL, AV_LOG_FATAL, "Sync stream specification in map %s does not "
                                    "match any streams.\n",
                arg);
         exit_program(1);
      }
   }

   if (map[0] == '[')
   {
      /* this mapping refers to lavfi output */
      const char *c = map + 1;
      GROW_ARRAY(o.stream_maps, o.nb_stream_maps);
      m = &o.stream_maps[o.nb_stream_maps - 1];
      m->linklabel = av_get_token(&c, "]");
      if (!m->linklabel)
      {
         av_log(NULL, AV_LOG_ERROR, "Invalid output link label: %s.\n", map);
         exit_program(1);
      }
   }
   else
   {
      if (allow_unused = strchr(map, '?'))
         *allow_unused = 0;
      file_idx = strtol(map, &p, 0);
      if (file_idx >= nb_input_files || file_idx < 0)
      {
         av_log(NULL, AV_LOG_FATAL, "Invalid input file index: %d.\n", file_idx);
         exit_program(1);
      }
      if (negative)
         /* disable some already defined maps */
         for (i = 0; i < o.nb_stream_maps; i++)
         {
            m = &o.stream_maps[i];
            if (file_idx == m->file_index &&
                check_stream_specifier(input_files[m->file_index]->ctx,
                                       input_files[m->file_index]->ctx->streams[m->stream_index],
                                       *p == ':' ? p + 1 : p) > 0)
               m->disabled = 1;
         }
      else
         for (i = 0; i < input_files[file_idx]->nb_streams; i++)
         {
            if (check_stream_specifier(input_files[file_idx]->ctx, input_files[file_idx]->ctx->streams[i],
                                       *p == ':' ? p + 1 : p) <= 0)
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
      {
         av_log(NULL, AV_LOG_FATAL, "Stream map '%s' matches no streams.\n"
                                    "To ignore this, add a trailing '?' to the map.\n",
                arg);
         exit_program(1);
      }
   }

   av_freep(&map);
   return 0;
}

static int opt_attach(void *optctx, const char *opt, const char *arg)
{
   OptionsContext *o = optctx;
   GROW_ARRAY(o.attachments, o.nb_attachments);
   o.attachments[o.nb_attachments - 1] = arg;
   return 0;
}

static int opt_map_channel(void *optctx, const char *opt, const char *arg)
{
   OptionsContext *o = optctx;
   int n;
   AVStream *st;
   AudioChannelMap *m;

   GROW_ARRAY(o.audio_channel_maps, o.nb_audio_channel_maps);
   m = &o.audio_channel_maps[o.nb_audio_channel_maps - 1];

   /* muted channel syntax */
   n = sscanf(arg, "%d:%d.%d", &m->channel_idx, &m->ofile_idx, &m->ostream_idx);
   if ((n == 1 || n == 3) && m->channel_idx == -1)
   {
      m->file_idx = m->stream_idx = -1;
      if (n == 1)
         m->ofile_idx = m->ostream_idx = -1;
      return 0;
   }

   /* normal syntax */
   n = sscanf(arg, "%d.%d.%d:%d.%d",
              &m->file_idx, &m->stream_idx, &m->channel_idx,
              &m->ofile_idx, &m->ostream_idx);

   if (n != 3 && n != 5)
   {
      av_log(NULL, AV_LOG_FATAL, "Syntax error, mapchan usage: "
                                 "[file.stream.channel|-1][:syncfile:syncstream]\n");
      exit_program(1);
   }

   if (n != 5) // only file.stream.channel specified
      m->ofile_idx = m->ostream_idx = -1;

   /* check input */
   if (m->file_idx < 0 || m->file_idx >= nb_input_files)
   {
      av_log(NULL, AV_LOG_FATAL, "mapchan: invalid input file index: %d\n",
             m->file_idx);
      exit_program(1);
   }
   if (m->stream_idx < 0 ||
       m->stream_idx >= input_files[m->file_idx]->nb_streams)
   {
      av_log(NULL, AV_LOG_FATAL, "mapchan: invalid input file stream index #%d.%d\n",
             m->file_idx, m->stream_idx);
      exit_program(1);
   }
   st = input_files[m->file_idx]->ctx->streams[m->stream_idx];
   if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
   {
      av_log(NULL, AV_LOG_FATAL, "mapchan: stream #%d.%d is not an audio stream.\n",
             m->file_idx, m->stream_idx);
      exit_program(1);
   }
   if (m->channel_idx < 0 || m->channel_idx >= st->codecpar->channels)
   {
      av_log(NULL, AV_LOG_FATAL, "mapchan: invalid audio channel #%d.%d.%d\n",
             m->file_idx, m->stream_idx, m->channel_idx);
      exit_program(1);
   }
   return 0;
}

static int opt_sdp_file(void *optctx, const char *opt, const char *arg)
{
   av_free(sdp_filename);
   sdp_filename = av_strdup(arg);
   return 0;
}

#if CONFIG_VAAPI
static int opt_vaapi_device(void *optctx, const char *opt, const char *arg)
{
   int err;
   err = vaapi_device_init(arg);
   if (err < 0)
      exit_program(1);
   return 0;
}
#endif

/**
 * Parse a metadata specifier passed as 'arg' parameter.
 * @param arg  metadata string to parse
 * @param type metadata type is written here -- g(lobal)/s(tream)/c(hapter)/p(rogram)
 * @param index for type c/p, chapter/program index is written here
 * @param stream_spec for type s, the stream specifier is written here
 */
static void parse_meta_type(char *arg, char *type, int *index, const char **stream_spec)
{
   if (*arg)
   {
      *type = *arg;
      switch (*arg)
      {
      case 'g':
         break;
      case 's':
         if (*(++arg) && *arg != ':')
         {
            av_log(NULL, AV_LOG_FATAL, "Invalid metadata specifier %s.\n", arg);
            exit_program(1);
         }
         *stream_spec = *arg == ':' ? arg + 1 : "";
         break;
      case 'c':
      case 'p':
         if (*(++arg) == ':')
            *index = strtol(++arg, NULL, 0);
         break;
      default:
         av_log(NULL, AV_LOG_FATAL, "Invalid metadata type %c.\n", *arg);
         exit_program(1);
      }
   }
   else
      *type = 'g';
}

static int copy_metadata(char *outspec, char *inspec, AVFormatContext *oc, AVFormatContext *ic, OptionsContext *o)
{
   AVDictionary **meta_in = NULL;
   AVDictionary **meta_out = NULL;
   int i, ret = 0;
   char type_in, type_out;
   const char *istream_spec = NULL, *ostream_spec = NULL;
   int idx_in = 0, idx_out = 0;

   parse_meta_type(inspec, &type_in, &idx_in, &istream_spec);
   parse_meta_type(outspec, &type_out, &idx_out, &ostream_spec);

   if (!ic)
   {
      if (type_out == 'g' || !*outspec)
         o.metadata_global_manual = 1;
      if (type_out == 's' || !*outspec)
         o.metadata_streams_manual = 1;
      if (type_out == 'c' || !*outspec)
         o.metadata_chapters_manual = 1;
      return 0;
   }

   if (type_in == 'g' || type_out == 'g')
      o.metadata_global_manual = 1;
   if (type_in == 's' || type_out == 's')
      o.metadata_streams_manual = 1;
   if (type_in == 'c' || type_out == 'c')
      o.metadata_chapters_manual = 1;

   /* ic is NULL when just disabling automatic mappings */
   if (!ic)
      return 0;

#define METADATA_CHECK_INDEX(index, nb_elems, desc)                                       \
   if ((index) < 0 || (index) >= (nb_elems))                                              \
   {                                                                                      \
      av_log(NULL, AV_LOG_FATAL, "Invalid %s index %d while processing metadata maps.\n", \
             (desc), (index));                                                            \
      exit_program(1);                                                                    \
   }

#define SET_DICT(type, meta, context, index)                       \
   switch (type)                                                   \
   {                                                               \
   case 'g':                                                       \
      meta = &context->metadata;                                   \
      break;                                                       \
   case 'c':                                                       \
      METADATA_CHECK_INDEX(index, context->nb_chapters, "chapter") \
      meta = &context->chapters[index]->metadata;                  \
      break;                                                       \
   case 'p':                                                       \
      METADATA_CHECK_INDEX(index, context->nb_programs, "program") \
      meta = &context->programs[index]->metadata;                  \
      break;                                                       \
   case 's':                                                       \
      break; /* handled separately below */                        \
   default:                                                        \
      av_assert0(0);                                               \
   }

   SET_DICT(type_in, meta_in, ic, idx_in);
   SET_DICT(type_out, meta_out, oc, idx_out);

   /* for input streams choose first matching stream */
   if (type_in == 's')
   {
      for (i = 0; i < ic->nb_streams; i++)
      {
         if ((ret = check_stream_specifier(ic, ic->streams[i], istream_spec)) > 0)
         {
            meta_in = &ic->streams[i]->metadata;
            break;
         }
         else if (ret < 0)
            exit_program(1);
      }
      if (!meta_in)
      {
         av_log(NULL, AV_LOG_FATAL, "Stream specifier %s does not match  any streams.\n", istream_spec);
         exit_program(1);
      }
   }

   if (type_out == 's')
   {
      for (i = 0; i < oc->nb_streams; i++)
      {
         if ((ret = check_stream_specifier(oc, oc->streams[i], ostream_spec)) > 0)
         {
            meta_out = &oc->streams[i]->metadata;
            av_dict_copy(meta_out, *meta_in, AV_DICT_DONT_OVERWRITE);
         }
         else if (ret < 0)
            exit_program(1);
      }
   }
   else
      av_dict_copy(meta_out, *meta_in, AV_DICT_DONT_OVERWRITE);

   return 0;
}

static int opt_recording_timestamp(void *optctx, const char *opt, const char *arg)
{
   OptionsContext *o = optctx;
   char buf[128];
   int64_t recording_timestamp = parse_time_or_die(opt, arg, 0) / 1E6;
   struct tm time = *gmtime((time_t *)&recording_timestamp);
   if (!strftime(buf, sizeof(buf), "creation_time=%Y-%m-%dT%H:%M:%S%z", &time))
      return -1;
   parse_option(o, "metadata", buf, options);

   av_log(NULL, AV_LOG_WARNING, "%s is deprecated, set the 'creation_time' metadata "
                                "tag instead.\n",
          opt);
   return 0;
}

InputFile open_input_file(OptionsContext &o, const char *filename, const int idx)
{
   // create new input file instance to be returned
   InputFile f(filename, idx, o);

   input_files.push_back(f);

   /* update the current parameters so that they match the one of the input stream */
   input_files.back().add_input_streams(input_streams); // append streams in this file to input_streams
   return 0;
}

static OutputStream &new_output_stream(OptionsContext &o, AVFormatContext *oc, AVMediaType type, InputStream *src = NULL)
{
   // create new output stream for the latest output file & add the stream reference to the output_streams
   output_streams.push_back(output_files.back().new_stream(o, oc, type, src));
   return output_streams.back();
}

static void parse_matrix_coeffs(uint16_t *dest, const char *str)
{
   int i;
   const char *p = str;
   for (i = 0;; i++)
   {
      dest[i] = atoi(p);
      if (i == 63)
         break;
      p = strchr(p, ',');
      if (!p)
      {
         av_log(NULL, AV_LOG_FATAL, "Syntax error in matrix \"%s\" at coeff %d\n", str, i);
         exit_program(1);
      }
      p++;
   }
}

/* read file contents into a string */
static uint8_t *read_file(const char *filename)
{
   AVIOContext *pb = NULL;
   AVIOContext *dyn_buf = NULL;
   int ret = avio_open(&pb, filename, AVIO_FLAG_READ);
   uint8_t buf[1024], *str;

   if (ret < 0)
   {
      av_log(NULL, AV_LOG_ERROR, "Error opening file %s.\n", filename);
      return NULL;
   }

   ret = avio_open_dyn_buf(&dyn_buf);
   if (ret < 0)
   {
      avio_closep(&pb);
      return NULL;
   }
   while ((ret = avio_read(pb, buf, sizeof(buf))) > 0)
      avio_write(dyn_buf, buf, ret);
   avio_w8(dyn_buf, 0);
   avio_closep(&pb);

   ret = avio_close_dyn_buf(dyn_buf, &str);
   if (ret < 0)
      return NULL;
   return str;
}

static char *get_ost_filters(OptionsContext *o, AVFormatContext *oc,
                             OutputStream *ost)
{
   AVStream *st = ost->st;

   if (ost->filters_script && ost->filters)
   {
      av_log(NULL, AV_LOG_ERROR, "Both -filter and -filter_script set for "
                                 "output stream #%d:%d.\n",
             nb_output_files, st->index);
      exit_program(1);
   }

   if (ost->filters_script)
      return read_file(ost->filters_script);
   else if (ost->filters)
      return av_strdup(ost->filters);

   return av_strdup(st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ? "null" : "anull");
}

static void check_streamcopy_filters(OptionsContext *o, AVFormatContext *oc,
                                     const OutputStream *ost, enum AVMediaType type)
{
   if (ost->filters_script || ost->filters)
   {
      av_log(NULL, AV_LOG_ERROR,
             "%s '%s' was defined for %s output stream %d:%d but codec copy was selected.\n"
             "Filtering and streamcopy cannot be used together.\n",
             ost->filters ? "Filtergraph" : "Filtergraph script",
             ost->filters ? ost->filters : ost->filters_script,
             av_get_media_type_string(type), ost->file_index, ost->index);
      exit_program(1);
   }
}

/* arg format is "output-stream-index:streamid-value". */
static int opt_streamid(void *optctx, const char *opt, const char *arg)
{
   OptionsContext *o = optctx;
   int idx;
   char *p;
   char idx_str[16];

   av_strlcpy(idx_str, arg, sizeof(idx_str));
   p = strchr(idx_str, ':');
   if (!p)
   {
      av_log(NULL, AV_LOG_FATAL,
             "Invalid value '%s' for option '%s', required syntax is 'index:value'\n",
             arg, opt);
      exit_program(1);
   }
   *p++ = '\0';
   idx = parse_number_or_die(opt, idx_str, OPT_INT, 0, MAX_STREAMS - 1);
   o.streamid_map = grow_array(o.streamid_map, sizeof(*o.streamid_map), &o.nb_streamid_map, idx + 1);
   o.streamid_map[idx] = parse_number_or_die(opt, p, OPT_INT, 0, INT_MAX);
   return 0;
}

static int copy_chapters(InputFile *ifile, OutputFile *ofile, int copy_metadata)
{
   AVFormatContext *is = ifile->ctx;
   AVFormatContext *os = ofile->ctx;
   AVChapter **tmp;
   int i;

   tmp = av_realloc_f(os->chapters, is->nb_chapters + os->nb_chapters, sizeof(*os->chapters));
   if (!tmp)
      return AVERROR(ENOMEM);
   os->chapters = tmp;

   for (i = 0; i < is->nb_chapters; i++)
   {
      AVChapter *in_ch = is->chapters[i], *out_ch;
      int64_t start_time = (ofile->start_time == AV_NOPTS_VALUE) ? 0 : ofile->start_time;
      int64_t ts_off = av_rescale_q(start_time - ifile->ts_offset,
                                    AV_TIME_BASE_Q, in_ch->time_base);
      int64_t rt = (ofile->recording_time == INT64_MAX) ? INT64_MAX : av_rescale_q(ofile->recording_time, AV_TIME_BASE_Q, in_ch->time_base);

      if (in_ch->end < ts_off)
         continue;
      if (rt != INT64_MAX && in_ch->start > rt + ts_off)
         break;

      out_ch = av_mallocz(sizeof(AVChapter));
      if (!out_ch)
         return AVERROR(ENOMEM);

      out_ch->id = in_ch->id;
      out_ch->time_base = in_ch->time_base;
      out_ch->start = FFMAX(0, in_ch->start - ts_off);
      out_ch->end = FFMIN(rt, in_ch->end - ts_off);

      if (copy_metadata)
         av_dict_copy(&out_ch->metadata, in_ch->metadata, 0);

      os->chapters[os->nb_chapters++] = out_ch;
   }
   return 0;
}

static void init_output_filter(OutputFilter *ofilter, OptionsContext *o,
                               AVFormatContext *oc)
{
   OutputStream *ost;

   switch (ofilter->type)
   {
   case AVMEDIA_TYPE_VIDEO:
      ost = new_video_stream(o, oc, -1);
      break;
   case AVMEDIA_TYPE_AUDIO:
      ost = new_audio_stream(o, oc, -1);
      break;
   default:
      av_log(NULL, AV_LOG_FATAL, "Only video and audio filters are supported "
                                 "currently.\n");
      exit_program(1);
   }

   ost->source_index = -1;
   ost->filter = ofilter;

   ofilter->ost = ost;

   if (ost->stream_copy)
   {
      av_log(NULL, AV_LOG_ERROR, "Streamcopy requested for output stream %d:%d, "
                                 "which is fed from a complex filtergraph. Filtering and streamcopy "
                                 "cannot be used together.\n",
             ost->file_index, ost->index);
      exit_program(1);
   }

   if (ost->avfilter && (ost->filters || ost->filters_script))
   {
      const char *opt = ost->filters ? "-vf/-af/-filter" : "-filter_script";
      av_log(NULL, AV_LOG_ERROR,
             "%s '%s' was specified through the %s option "
             "for output stream %d:%d, which is fed from a complex filtergraph.\n"
             "%s and -filter_complex cannot be used together for the same stream.\n",
             ost->filters ? "Filtergraph" : "Filtergraph script",
             ost->filters ? ost->filters : ost->filters_script,
             opt, ost->file_index, ost->index, opt);
      exit_program(1);
   }

   avfilter_inout_free(&ofilter->out_tmp);
}

static int init_complex_filters(void)
{
   for (FilterGraphs::iterator it = filtergraphs.begin(); it < filtergraphs.end(); it++)
   {
      if (int ret = it->init_complex_filtergraph() < 0)
         return ret;
   }
   return 0;
}

static int configure_complex_filters(void)
{
   for (FilterGraphs::iterator it = filtergraphs.begin(); it < filtergraphs.end(); it++)
      if (!it->filtergraph_is_simple()) && (int ret = it->configure_filtergraph()) < 0)
            return ret;
   return 0;
}

static int open_output_file(OptionsContext &o, const std::string filename)
{
   // open new output file
   output_files.emplace_back(filename, output_files.size(), o);

   OutputFile &of = output_files.back();
   AVFormatContext *oc = of.ctx;
   AVOutputFormat *file_oformat = oc->oformat;

   /* create streams for all unlabeled output pads */
   for (FilterGraphs::iterator fg = filtergraphs.begin(); fg < filtergraphs.end(); fg++)
   {
      for (OutputFilters::iterator ofilter = fg->outputs.begin(); ofilter < fg->outputs.end(); ofilter++)
      {
         // if output filter is not yet finalized
         if (!ofilter->out_tmp || ofilter->out_tmp->name)
            continue;

         switch (ofilter->type)
         {
         case AVMEDIA_TYPE_VIDEO:
            o.video_disable = 1;
            break;
         case AVMEDIA_TYPE_AUDIO:
            o.audio_disable = 1;
            break;
         case AVMEDIA_TYPE_SUBTITLE:
            o.subtitle_disable = 1;
            break;
         }
         ofilter.init_output_filter(o, oc);
      }
   }

   // map streams
   if (o.stream_maps.empty())
   {
      /* pick the "best" stream of each type */

      /* video: highest resolution */
      if (!o.video_disable && av_guess_codec(oc->oformat, NULL, filename.c_str(), NULL, AVMEDIA_TYPE_VIDEO) != AV_CODEC_ID_NONE)
      {
         InputStream *ist_matched = NULL;
         int area = 0;
         int qcr = avformat_query_codec(oc->oformat, oc->oformat->video_codec, 0);
         for (auto ist = input_streams.begin(); ist < input_streams.end(); ist++)
         {
            int new_area;
            new_area = ist->st->codecpar->width * ist->st->codecpar->height + 100000000 * !!ist->st->codec_info_nb_frames;
            if ((qcr != MKTAG('A', 'P', 'I', 'C')) && (ist->st->disposition & AV_DISPOSITION_ATTACHED_PIC))
               new_area = 1;
            if (ist->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && new_area > area)
            {
               if ((qcr == MKTAG('A', 'P', 'I', 'C')) && !(ist->st->disposition & AV_DISPOSITION_ATTACHED_PIC))
                  continue;
               area = new_area;
               ist_matched = &*ist;
            }
         }
         if (ist_matched)
            new_video_stream(o, oc, ist_matched);
      }

      /* audio: most channels */
      if (!o.audio_disable && av_guess_codec(oc->oformat, NULL, filename.c_str(), NULL, AVMEDIA_TYPE_AUDIO) != AV_CODEC_ID_NONE)
      {
         InputStream *ist_matched = NULL;
         int best_score = 0;
         for (auto ist = input_streams.begin(); ist < input_stream.end(); ist++)
         {
            int score;
            score = ist->st->codecpar->channels + 100000000 * !!ist->st->codec_info_nb_frames;
            if (ist->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && score > best_score)
            {
               ist_matched = &*ist;
               best_score = score;
            }
         }
         if (ist_matched)
            new_audio_stream(o, oc, ist_matched);
      }

      /* subtitles: pick first */
      std::string subtitle_codec_name;
      MATCH_PER_TYPE_OPT(codec_names, str, subtitle_codec_name, oc, "s");
      if (!o.subtitle_disable && (avcodec_find_encoder(oc->oformat->subtitle_codec) || subtitle_codec_name))
      {
         for (auto ist = input_streams.begin(); ist < input_streams.end(); ist++)
            if (ist->st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
               AVCodecDescriptor const *input_descriptor = avcodec_descriptor_get(input_streams[i]->st->codecpar->codec_id);
               AVCodecDescriptor const *output_descriptor = NULL;
               AVCodec const *output_codec = avcodec_find_encoder(oc->oformat->subtitle_codec);
               int input_props = 0, output_props = 0;
               if (output_codec)
                  output_descriptor = avcodec_descriptor_get(output_codec->id);
               if (input_descriptor)
                  input_props = input_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
               if (output_descriptor)
                  output_props = output_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
               // Map dvb teletext which has neither property to any output subtitle encoder
               if (subtitle_codec_name || input_props & output_props || input_descriptor && output_descriptor && (!input_descriptor->props || !output_descriptor->props))
               {
                  new_subtitle_stream(o, oc, &*ist);
                  break;
               }
            }
      }

      /* Data only if codec id match */
      if (!o.data_disable)
      {
         enum AVCodecID codec_id = av_guess_codec(oc->oformat, NULL, filename, NULL, AVMEDIA_TYPE_DATA);
         for (auto ist = input_streams.begin(); codec_id != AV_CODEC_ID_NONE && ist < input_streams.end(); ist++)
         {
            if (ist->st->codecpar->codec_type == AVMEDIA_TYPE_DATA && ist->st->codecpar->codec_id == codec_id)
               new_data_stream(o, oc, &*ist);
         }
      }
   }
   else
   {
      for (auto map = o.stream_maps.begin(); map < o.stream_maps.end(); map++)
      {
         if (map->disabled)
            continue;

         if (map->linklabel)
         {
            OutputFilters::iterator ofilter;
            for (auto fg = filtergraphs.begin(); fg < filtergraphs.end(); fg++)
            {
               for (ofilter = fg->outputs.begin(); ofilter < fg->outputs.end(); ofilter++)
               {
                  AVFilterInOut *out = ofilter->out_tmp;
                  if (out && !strcmp(out->name, map->linklabel))
                  {
                     ofilter = fg->outputs[k];
                     goto loop_end;
                  }
               }
            }
         loop_end:
            if (ofilter == fg->outputs.end())
            {
               std::ostringstream msg << "Output with label '" << map->linklabel << "' does not exist in any defined filter graph, or was already used elsewhere.";
               throw ffmpegException(msg.str());
            }
            ofilter->init_output_filter(o, oc);
         }
         else
         {
            int src_idx = input_files[map->file_index]->ist_index + map->stream_index;

            InputStream &ist = input_files[map->file_index].streams[map->stream_index];
            if (o.subtitle_disable && ist.st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
               continue;
            if (o.audio_disable && ist.st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
               continue;
            if (o.video_disable && ist.st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
               continue;
            if (o.data_disable && ist.st->codecpar->codec_type == AVMEDIA_TYPE_DATA)
               continue;

            OutputStream *ost = NULL;
            switch (ist.st->codecpar->codec_type)
            {
            case AVMEDIA_TYPE_VIDEO:
               ost = new_video_stream(o, oc, ist);
               break;
            case AVMEDIA_TYPE_AUDIO:
               ost = new_audio_stream(o, oc, ist);
               break;
            case AVMEDIA_TYPE_SUBTITLE:
               ost = new_subtitle_stream(o, oc, ist);
               break;
            case AVMEDIA_TYPE_DATA:
               ost = new_data_stream(o, oc, ist);
               break;
            case AVMEDIA_TYPE_ATTACHMENT:
               ost = new_attachment_stream(o, oc, ist);
               break;
            case AVMEDIA_TYPE_UNKNOWN:
               if (copy_unknown_streams)
               {
                  ost = new_unknown_stream(o, oc, ist);
                  break;
               }
            default:
               av_log(NULL, ignore_unknown_streams ? AV_LOG_WARNING : AV_LOG_FATAL,
                      "Cannot map stream #%d:%d - unsupported type.\n",
                      map->file_index, map->stream_index);
               if (!ignore_unknown_streams)
               {
                  throw ffmpegException("If you want unsupported types ignored instead "
                                        "of failing, please use the -ignore_unknown option\n"
                                        "If you want them copied, please use -copy_unknown");
               }
            }
            if (ost)
               ost->sync_ist = ist;
         }
      }
   }

   /* handle attached files */
   for (auto attachment = o.attachments.begin(); attachment < o.attachments.end(); attachment++)
   {
      AVIOContext *pb;
      const char *p;
      int64_t len;

      if (avio_open2(&pb, attachment->c_str(), AVIO_FLAG_READ, &ffmpegBase::int_cb, NULL) < 0)
      {
         std::ostringstream msg << "Could not open attachment file " << *&attachment << ".";
         throw ffmpegException(msg.str());
      }
      if ((len = avio_size(pb)) <= 0)
      {
         std::ostringstream msg << "Could not get size of the attachment " << *&attachment << ".";
         throw ffmpegException(msg.str());
      }
      if (!(attachment = av_malloc(len)))
      {
         std::ostringstream msg << "Attachment " << *&attachment << " too large to fit into memory.";
      }
      avio_read(pb, attachment, len);

      OutputStream *ost = new_attachment_stream(o, oc, NULL);
      ost.stream_copy = 0;
      ost.attachment_filename = *attachment;
      ost.st->codecpar->extradata = attachment->c_str();
      ost.st->codecpar->extradata_size = len;

      p = strrchr(attachment.c_str(), '/');
      av_dict_set(&ost.st->metadata, "filename", (p && *p) ? p + 1 : attachment->c_str(), AV_DICT_DONT_OVERWRITE);
      avio_closep(&pb);
   }

   if (!oc->nb_streams && !(oc->oformat->flags & AVFMT_NOSTREAMS))
   {
      //av_dump_format(oc, nb_output_files - 1, oc->filename, 1);
      std::ostringstream msg << "Output file #" << output_files.size() - 1 << " does not contain any stream";
      throw ffmpegException(msg.str());
   }

   /* check if all codec options have been used */
   AVDictionary *unused_opts = NULL;
   AVDictionaryEntry *e = NULL;
   unused_opts = strip_specifiers(o.g->codec_opts);
   for (i = of->ost_index; i < nb_output_streams; i++)
   {
      e = NULL;
      while ((e = av_dict_get(output_streams[i]->encoder_opts, "", e,
                              AV_DICT_IGNORE_SUFFIX)))
         av_dict_set(&unused_opts, e->key, NULL, 0);
   }

   e = NULL;
   while ((e = av_dict_get(unused_opts, "", e, AV_DICT_IGNORE_SUFFIX)))
   {
      const AVClass *class = avcodec_get_class();
      const AVOption *option = av_opt_find(&class, e->key, NULL, 0,
                                           AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
      const AVClass *fclass = avformat_get_class();
      const AVOption *foption = av_opt_find(&fclass, e->key, NULL, 0,
                                            AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
      if (!option || foption)
         continue;

      if (!(option->flags & AV_OPT_FLAG_ENCODING_PARAM))
      {
         av_log(NULL, AV_LOG_ERROR, "Codec AVOption %s (%s) specified for "
                                    "output file #%d (%s) is not an encoding option.\n",
                e->key,
                option->help ? option->help : "", nb_output_files - 1,
                filename);
         exit_program(1);
      }

      // gop_timecode is injected by generic code but not always used
      if (!strcmp(e->key, "gop_timecode"))
         continue;

      av_log(NULL, AV_LOG_WARNING, "Codec AVOption %s (%s) specified for "
                                   "output file #%d (%s) has not been used for any stream. The most "
                                   "likely reason is either wrong type (e.g. a video option with "
                                   "no video streams) or that it is a private option of some encoder "
                                   "which was not actually used for any stream.\n",
             e->key,
             option->help ? option->help : "", nb_output_files - 1, filename);
   }
   av_dict_free(&unused_opts);

   /* set the decoding_needed flags and create simple filtergraphs */
   for (auto ost = of.streams.begin(); ost < of.streams.end(); ost++)
   {
      if (ost->encoding_needed && ost->source_index >= 0)
      {
         InputStream *ist = ost->sync_ist;
         ist->decoding_needed |= DECODING_FOR_OST;

         if (ost->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO || ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            filtergraphs.emplace_back(ist, &*ost);
      }
   }

   /* check filename in case of an image number is expected */
   if (oc->oformat->flags & AVFMT_NEEDNUMBER)
   {
      if (!av_filename_number_test(oc->filename))
      {
         print_error(oc->filename, AVERROR(EINVAL));
         exit_program(1);
      }
   }

   if (!(oc->oformat->flags & AVFMT_NOSTREAMS) && !input_stream_potentially_available)
   {
      av_log(NULL, AV_LOG_ERROR,
             "No input streams but output needs an input stream\n");
      exit_program(1);
   }

   if (!(oc->oformat->flags & AVFMT_NOFILE))
   {
      /* test if it already exists to avoid losing precious files */
      assert_file_overwrite(filename);

      /* open the file */
      if ((int err = avio_open2(&oc->pb, filename, AVIO_FLAG_WRITE,
                                &oc->interrupt_callback,
                                &of->opts)) < 0)
         throw ffmpegException(filename, err);
   }
   else if (strcmp(oc->oformat->name, "image2") == 0 && !av_filename_number_test(filename))
      assert_file_overwrite(filename);

   if (o.mux_preload)
   {
      av_dict_set_int(&of->opts, "preload", o.mux_preload * AV_TIME_BASE, 0);
   }
   oc->max_delay = (int)(o.mux_max_delay * AV_TIME_BASE);

   /* copy metadata */
   for (i = 0; i < o.nb_metadata_map; i++)
   {
      char *p;
      int in_file_index = strtol(o.metadata_map[i].u.str, &p, 0);

      if (in_file_index >= nb_input_files)
      {
         av_log(NULL, AV_LOG_FATAL, "Invalid input file index %d while processing metadata maps\n", in_file_index);
         exit_program(1);
      }
      copy_metadata(o.metadata_map[i].specifier, *p ? p + 1 : p, oc,
                    in_file_index >= 0 ? input_files[in_file_index]->ctx : NULL, o);
   }

   /* copy chapters */
   if (o.chapters_input_file >= nb_input_files)
   {
      if (o.chapters_input_file == INT_MAX)
      {
         /* copy chapters from the first input file that has them*/
         o.chapters_input_file = -1;
         for (i = 0; i < nb_input_files; i++)
            if (input_files[i]->ctx->nb_chapters)
            {
               o.chapters_input_file = i;
               break;
            }
      }
      else
      {
         av_log(NULL, AV_LOG_FATAL, "Invalid input file index %d in chapter mapping.\n",
                o.chapters_input_file);
         exit_program(1);
      }
   }
   if (o.chapters_input_file >= 0)
      copy_chapters(input_files[o.chapters_input_file], of,
                    !o.metadata_chapters_manual);

   /* copy global metadata by default */
   if (!o.metadata_global_manual && nb_input_files)
   {
      av_dict_copy(&oc->metadata, input_files[0]->ctx->metadata,
                   AV_DICT_DONT_OVERWRITE);
      if (o.recording_time != INT64_MAX)
         av_dict_set(&oc->metadata, "duration", NULL, 0);
      av_dict_set(&oc->metadata, "creation_time", NULL, 0);
   }
   if (!o.metadata_streams_manual)
      for (i = of->ost_index; i < nb_output_streams; i++)
      {
         InputStream *ist;
         if (output_streams[i]->source_index < 0) /* this is true e.g. for attached files */
            continue;
         ist = input_streams[output_streams[i]->source_index];
         av_dict_copy(&output_streams[i]->st->metadata, ist->st->metadata, AV_DICT_DONT_OVERWRITE);
         if (!output_streams[i]->stream_copy)
         {
            av_dict_set(&output_streams[i]->st->metadata, "encoder", NULL, 0);
            if (ist->autorotate)
               av_dict_set(&output_streams[i]->st->metadata, "rotate", NULL, 0);
         }
      }

   /* process manually set programs */
   for (i = 0; i < o.nb_program; i++)
   {
      const char *p = o.program[i].u.str;
      int progid = i + 1;
      AVProgram *program;

      while (*p)
      {
         const char *p2 = av_get_token(&p, ":");
         const char *to_dealloc = p2;
         char *key;
         if (!p2)
            break;

         if (*p)
            p++;

         key = av_get_token(&p2, "=");
         if (!key || !*p2)
         {
            av_freep(&to_dealloc);
            av_freep(&key);
            break;
         }
         p2++;

         if (!strcmp(key, "program_num"))
            progid = strtol(p2, NULL, 0);
         av_freep(&to_dealloc);
         av_freep(&key);
      }

      program = av_new_program(oc, progid);

      p = o.program[i].u.str;
      while (*p)
      {
         const char *p2 = av_get_token(&p, ":");
         const char *to_dealloc = p2;
         char *key;
         if (!p2)
            break;
         if (*p)
            p++;

         key = av_get_token(&p2, "=");
         if (!key)
         {
            av_log(NULL, AV_LOG_FATAL,
                   "No '=' character in program string %s.\n",
                   p2);
            exit_program(1);
         }
         if (!*p2)
            exit_program(1);
         p2++;

         if (!strcmp(key, "title"))
         {
            av_dict_set(&program->metadata, "title", p2, 0);
         }
         else if (!strcmp(key, "program_num"))
         {
         }
         else if (!strcmp(key, "st"))
         {
            int st_num = strtol(p2, NULL, 0);
            av_program_add_stream_index(oc, progid, st_num);
         }
         else
         {
            av_log(NULL, AV_LOG_FATAL, "Unknown program key %s.\n", key);
            exit_program(1);
         }
         av_freep(&to_dealloc);
         av_freep(&key);
      }
   }

   /* process manually set metadata */
   for (auto meta = o.metadata.begin(); meta < o.metadata.end(); meta++)
   {
      AVDictionary **m;
      char type, *val;
      const char *stream_spec;
      int index = 0, j, ret = 0;

      val = strchr(meta->u.str->c_str(), '=');
      if (!val)
      {
         std::ostringstream msg << "No '=' character in metadata string " << meta->u.str << ".";
         throw ffmpegException(msg.str());
      }
      *val++ = 0;

      parse_meta_type(meta->specifier, &type, &index, &stream_spec);
      if (type == 's')
      {
         for (auto ost = of.streams.begin(); ost < of.streams.end(); ost++)
         {
            if ((ret = check_stream_specifier(oc, ost->st, stream_spec)) > 0)
            {
               av_dict_set(&ost->st->metadata, meta->u.str, *val ? val : NULL, 0);
               if (meta->u.str == "rotate")
               {
                  ost->rotate_overridden = 1;
               }
            }
            else if (ret < 0)
               throw ffmpegException("Invalid meta data specifier");
         }
      }
      else
      {
         switch (type)
         {
         case 'g':
            m = &oc->metadata;
            break;
         case 'c':
            if (index < 0 || index >= oc->nb_chapters)
            {
               av_log(NULL, AV_LOG_FATAL, "Invalid chapter index %d in metadata specifier.\n", index);
               exit_program(1);
            }
            m = &oc->chapters[index]->metadata;
            break;
         case 'p':
            if (index < 0 || index >= oc->nb_programs)
            {
               av_log(NULL, AV_LOG_FATAL, "Invalid program index %d in metadata specifier.\n", index);
               exit_program(1);
            }
            m = &oc->programs[index]->metadata;
            break;
         default:
            av_log(NULL, AV_LOG_FATAL, "Invalid metadata specifier %s.\n", o.metadata[i].specifier);
            exit_program(1);
         }
         av_dict_set(m, meta->u.str, *val ? val : NULL, 0);
      }
   }

   return 0;
}

static int opt_target(void *optctx, const char *opt, const char *arg)
{
   OptionsContext *o = optctx;
   enum
   {
      PAL,
      NTSC,
      FILM,
      UNKNOWN
   } norm = UNKNOWN;
   static const char *const frame_rates[] = {"25", "30000/1001", "24000/1001"};

   if (!strncmp(arg, "pal-", 4))
   {
      norm = PAL;
      arg += 4;
   }
   else if (!strncmp(arg, "ntsc-", 5))
   {
      norm = NTSC;
      arg += 5;
   }
   else if (!strncmp(arg, "film-", 5))
   {
      norm = FILM;
      arg += 5;
   }
   else
   {
      /* Try to determine PAL/NTSC by peeking in the input files */
      if (nb_input_files)
      {
         int i, j, fr;
         for (j = 0; j < nb_input_files; j++)
         {
            for (i = 0; i < input_files[j]->nb_streams; i++)
            {
               AVStream *st = input_files[j]->ctx->streams[i];
               if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
                  continue;
               fr = st->time_base.den * 1000 / st->time_base.num;
               if (fr == 25000)
               {
                  norm = PAL;
                  break;
               }
               else if ((fr == 29970) || (fr == 23976))
               {
                  norm = NTSC;
                  break;
               }
            }
            if (norm != UNKNOWN)
               break;
         }
      }
      if (norm != UNKNOWN)
         av_log(NULL, AV_LOG_INFO, "Assuming %s for target.\n", norm == PAL ? "PAL" : "NTSC");
   }

   if (norm == UNKNOWN)
   {
      av_log(NULL, AV_LOG_FATAL, "Could not determine norm (PAL/NTSC/NTSC-Film) for target.\n");
      av_log(NULL, AV_LOG_FATAL, "Please prefix target with \"pal-\", \"ntsc-\" or \"film-\",\n");
      av_log(NULL, AV_LOG_FATAL, "or set a framerate with \"-r xxx\".\n");
      exit_program(1);
   }

   if (!strcmp(arg, "vcd"))
   {
      opt_video_codec(o, "c:v", "mpeg1video");
      opt_audio_codec(o, "c:a", "mp2");
      parse_option(o, "f", "vcd", options);

      parse_option(o, "s", norm == PAL ? "352x288" : "352x240", options);
      parse_option(o, "r", frame_rates[norm], options);
      opt_default(NULL, "g", norm == PAL ? "15" : "18");

      opt_default(NULL, "b:v", "1150000");
      opt_default(NULL, "maxrate:v", "1150000");
      opt_default(NULL, "minrate:v", "1150000");
      opt_default(NULL, "bufsize:v", "327680"); // 40*1024*8;

      opt_default(NULL, "b:a", "224000");
      parse_option(o, "ar", "44100", options);
      parse_option(o, "ac", "2", options);

      opt_default(NULL, "packetsize", "2324");
      opt_default(NULL, "muxrate", "1411200"); // 2352 * 75 * 8;

      /* We have to offset the PTS, so that it is consistent with the SCR.
           SCR starts at 36000, but the first two packs contain only padding
           and the first pack from the other stream, respectively, may also have
           been written before.
           So the real data starts at SCR 36000+3*1200. */
      o.mux_preload = (36000 + 3 * 1200) / 90000.0; // 0.44
   }
   else if (!strcmp(arg, "svcd"))
   {

      opt_video_codec(o, "c:v", "mpeg2video");
      opt_audio_codec(o, "c:a", "mp2");
      parse_option(o, "f", "svcd", options);

      parse_option(o, "s", norm == PAL ? "480x576" : "480x480", options);
      parse_option(o, "r", frame_rates[norm], options);
      parse_option(o, "pix_fmt", "yuv420p", options);
      opt_default(NULL, "g", norm == PAL ? "15" : "18");

      opt_default(NULL, "b:v", "2040000");
      opt_default(NULL, "maxrate:v", "2516000");
      opt_default(NULL, "minrate:v", "0");       // 1145000;
      opt_default(NULL, "bufsize:v", "1835008"); // 224*1024*8;
      opt_default(NULL, "scan_offset", "1");

      opt_default(NULL, "b:a", "224000");
      parse_option(o, "ar", "44100", options);

      opt_default(NULL, "packetsize", "2324");
   }
   else if (!strcmp(arg, "dvd"))
   {

      opt_video_codec(o, "c:v", "mpeg2video");
      opt_audio_codec(o, "c:a", "ac3");
      parse_option(o, "f", "dvd", options);

      parse_option(o, "s", norm == PAL ? "720x576" : "720x480", options);
      parse_option(o, "r", frame_rates[norm], options);
      parse_option(o, "pix_fmt", "yuv420p", options);
      opt_default(NULL, "g", norm == PAL ? "15" : "18");

      opt_default(NULL, "b:v", "6000000");
      opt_default(NULL, "maxrate:v", "9000000");
      opt_default(NULL, "minrate:v", "0");       // 1500000;
      opt_default(NULL, "bufsize:v", "1835008"); // 224*1024*8;

      opt_default(NULL, "packetsize", "2048");  // from www.mpucoder.com: DVD sectors contain 2048 bytes of data, this is also the size of one pack.
      opt_default(NULL, "muxrate", "10080000"); // from mplex project: data_rate = 1260000. mux_rate = data_rate * 8

      opt_default(NULL, "b:a", "448000");
      parse_option(o, "ar", "48000", options);
   }
   else if (!strncmp(arg, "dv", 2))
   {

      parse_option(o, "f", "dv", options);

      parse_option(o, "s", norm == PAL ? "720x576" : "720x480", options);
      parse_option(o, "pix_fmt", !strncmp(arg, "dv50", 4) ? "yuv422p" : norm == PAL ? "yuv420p" : "yuv411p", options);
      parse_option(o, "r", frame_rates[norm], options);

      parse_option(o, "ar", "48000", options);
      parse_option(o, "ac", "2", options);
   }
   else
   {
      av_log(NULL, AV_LOG_ERROR, "Unknown target: %s\n", arg);
      return AVERROR(EINVAL);
   }

   av_dict_copy(&o.g->codec_opts, codec_opts, AV_DICT_DONT_OVERWRITE);
   av_dict_copy(&o.g->format_opts, format_opts, AV_DICT_DONT_OVERWRITE);

   return 0;
}

static int opt_vstats_file(void *optctx, const char *opt, const char *arg)
{
   av_free(vstats_filename);
   vstats_filename = av_strdup(arg);
   return 0;
}

static int opt_vstats(void *optctx, const char *opt, const char *arg)
{
   char filename[40];
   time_t today2 = time(NULL);
   struct tm *today = localtime(&today2);

   if (!today)
   { // maybe tomorrow
      av_log(NULL, AV_LOG_FATAL, "Unable to get current time: %s\n", strerror(errno));
      exit_program(1);
   }

   snprintf(filename, sizeof(filename), "vstats_%02d%02d%02d.log", today->tm_hour, today->tm_min,
            today->tm_sec);
   return opt_vstats_file(NULL, opt, filename);
}

static int opt_default_new(OptionsContext *o, const char *opt, const char *arg)
{
   int ret;
   AVDictionary *cbak = codec_opts;
   AVDictionary *fbak = format_opts;
   codec_opts = NULL;
   format_opts = NULL;

   ret = opt_default(NULL, opt, arg);

   av_dict_copy(&o.g->codec_opts, codec_opts, 0);
   av_dict_copy(&o.g->format_opts, format_opts, 0);
   av_dict_free(&codec_opts);
   av_dict_free(&format_opts);
   codec_opts = cbak;
   format_opts = fbak;

   return ret;
}

static int opt_preset(void *optctx, const char *opt, const char *arg)
{
   OptionsContext *o = optctx;
   FILE *f = NULL;
   char filename[1000], line[1000], tmp_line[1000];
   const char *codec_name = NULL;

   tmp_line[0] = *opt;
   tmp_line[1] = 0;
   MATCH_PER_TYPE_OPT(codec_names, str, codec_name, NULL, tmp_line);

   if (!(f = get_preset_file(filename, sizeof(filename), arg, *opt == 'f', codec_name)))
   {
      if (!strncmp(arg, "libx264-lossless", strlen("libx264-lossless")))
      {
         av_log(NULL, AV_LOG_FATAL, "Please use -preset <speed> -qp 0\n");
      }
      else
         av_log(NULL, AV_LOG_FATAL, "File for preset '%s' not found\n", arg);
      exit_program(1);
   }

   while (fgets(line, sizeof(line), f))
   {
      char *key = tmp_line, *value, *endptr;

      if (strcspn(line, "#\n\r") == 0)
         continue;
      av_strlcpy(tmp_line, line, sizeof(tmp_line));
      if (!av_strtok(key, "=", &value) ||
          !av_strtok(value, "\r\n", &endptr))
      {
         av_log(NULL, AV_LOG_FATAL, "%s: Invalid syntax: '%s'\n", filename, line);
         exit_program(1);
      }
      av_log(NULL, AV_LOG_DEBUG, "ffpreset[%s]: set '%s' = '%s'\n", filename, key, value);

      if (!strcmp(key, "acodec"))
         opt_audio_codec(o, key, value);
      else if (!strcmp(key, "vcodec"))
         opt_video_codec(o, key, value);
      else if (!strcmp(key, "scodec"))
         opt_subtitle_codec(o, key, value);
      else if (!strcmp(key, "dcodec"))
         opt_data_codec(o, key, value);
      else if (opt_default_new(o, key, value) < 0)
      {
         av_log(NULL, AV_LOG_FATAL, "%s: Invalid option or argument: '%s', parsed as '%s' = '%s'\n",
                filename, line, key, value);
         exit_program(1);
      }
   }

   fclose(f);

   return 0;
}

static int opt_old2new(void *optctx, const char *opt, const char *arg)
{
   OptionsContext *o = optctx;
   char *s = av_asprintf("%s:%c", opt + 1, *opt);
   int ret = parse_option(o, s, arg, options);
   av_free(s);
   return ret;
}

static int opt_bitrate(void *optctx, const char *opt, const char *arg)
{
   OptionsContext *o = optctx;

   if (!strcmp(opt, "ab"))
   {
      av_dict_set(&o.g->codec_opts, "b:a", arg, 0);
      return 0;
   }
   else if (!strcmp(opt, "b"))
   {
      av_log(NULL, AV_LOG_WARNING, "Please use -b:a or -b:v, -b is ambiguous\n");
      av_dict_set(&o.g->codec_opts, "b:v", arg, 0);
      return 0;
   }
   av_dict_set(&o.g->codec_opts, opt, arg, 0);
   return 0;
}

static int opt_qscale(void *optctx, const char *opt, const char *arg)
{
   OptionsContext *o = optctx;
   char *s;
   int ret;
   if (!strcmp(opt, "qscale"))
   {
      av_log(NULL, AV_LOG_WARNING, "Please use -q:a or -q:v, -qscale is ambiguous\n");
      return parse_option(o, "q:v", arg, options);
   }
   s = av_asprintf("q%s", opt + 6);
   ret = parse_option(o, s, arg, options);
   av_free(s);
   return ret;
}

static int opt_profile(void *optctx, const char *opt, const char *arg)
{
   OptionsContext *o = optctx;
   if (!strcmp(opt, "profile"))
   {
      av_log(NULL, AV_LOG_WARNING, "Please use -profile:a or -profile:v, -profile is ambiguous\n");
      av_dict_set(&o.g->codec_opts, "profile:v", arg, 0);
      return 0;
   }
   av_dict_set(&o.g->codec_opts, opt, arg, 0);
   return 0;
}

static int opt_vsync(void *optctx, const char *opt, const char *arg)
{
   if (!av_strcasecmp(arg, "cfr"))
      video_sync_method = VSYNC_CFR;
   else if (!av_strcasecmp(arg, "vfr"))
      video_sync_method = VSYNC_VFR;
   else if (!av_strcasecmp(arg, "passthrough"))
      video_sync_method = VSYNC_PASSTHROUGH;
   else if (!av_strcasecmp(arg, "drop"))
      video_sync_method = VSYNC_DROP;

   if (video_sync_method == VSYNC_AUTO)
      video_sync_method = parse_number_or_die("vsync", arg, OPT_INT, VSYNC_AUTO, VSYNC_VFR);
   return 0;
}

static int opt_timecode(void *optctx, const char *opt, const char *arg)
{
   OptionsContext *o = optctx;
   char *tcr = av_asprintf("timecode=%s", arg);
   int ret = parse_option(o, "metadata:g", tcr, options);
   if (ret >= 0)
      ret = av_dict_set(&o.g->codec_opts, "gop_timecode", arg, 0);
   av_free(tcr);
   return 0;
}

static int opt_filter_complex(void *optctx, const char *opt, const char *arg)
{
   filtergraphs.emplace_back(filtergraphs.size(), arg);
   input_stream_potentially_available = 1;
   return 0;
}

static int opt_filter_complex_script(void *optctx, const char *opt, const char *arg)
{
   uint8_t *graph_desc = read_file(arg);
   if (!graph_desc)
      return AVERROR(EINVAL);
   filtergraphs.emplace_back(filtergraphs.size(), graph_desc);
   input_stream_potentially_available = 1;

   return 0;
}

void show_help_default(const char *opt, const char *arg)
{
   /* per-file options have at least one of those set */
   const int per_file = OPT_SPEC | OPT_OFFSET | OPT_PERFILE;
   int show_advanced = 0, show_avoptions = 0;

   if (opt && *opt)
   {
      if (!strcmp(opt, "long"))
         show_advanced = 1;
      else if (!strcmp(opt, "full"))
         show_advanced = show_avoptions = 1;
      else
         av_log(NULL, AV_LOG_ERROR, "Unknown help option '%s'.\n", opt);
   }

   show_usage();

   printf("Getting help:\n"
          "    -h      -- print basic options\n"
          "    -h long -- print more options\n"
          "    -h full -- print all options (including all format and codec specific options, very long)\n"
          "    -h type=name -- print all options for the named decoder/encoder/demuxer/muxer/filter\n"
          "    See man %s for detailed description of the options.\n"
          "\n",
          program_name);

   show_help_options(options, "Print help / information / capabilities:",
                     OPT_EXIT, 0, 0);

   show_help_options(options, "Global options (affect whole program "
                              "instead of just one file:",
                     0, per_file | OPT_EXIT | OPT_EXPERT, 0);
   if (show_advanced)
      show_help_options(options, "Advanced global options:", OPT_EXPERT,
                        per_file | OPT_EXIT, 0);

   show_help_options(options, "Per-file main options:", 0,
                     OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_SUBTITLE |
                         OPT_EXIT,
                     per_file);
   if (show_advanced)
      show_help_options(options, "Advanced per-file options:",
                        OPT_EXPERT, OPT_AUDIO | OPT_VIDEO | OPT_SUBTITLE, per_file);

   show_help_options(options, "Video options:",
                     OPT_VIDEO, OPT_EXPERT | OPT_AUDIO, 0);
   if (show_advanced)
      show_help_options(options, "Advanced Video options:",
                        OPT_EXPERT | OPT_VIDEO, OPT_AUDIO, 0);

   show_help_options(options, "Audio options:",
                     OPT_AUDIO, OPT_EXPERT | OPT_VIDEO, 0);
   if (show_advanced)
      show_help_options(options, "Advanced Audio options:",
                        OPT_EXPERT | OPT_AUDIO, OPT_VIDEO, 0);
   show_help_options(options, "Subtitle options:",
                     OPT_SUBTITLE, 0, 0);
   printf("\n");

   if (show_avoptions)
   {
      int flags = AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_ENCODING_PARAM;
      show_help_children(avcodec_get_class(), flags);
      show_help_children(avformat_get_class(), flags);
#if CONFIG_SWSCALE
      show_help_children(sws_get_class(), flags);
#endif
      show_help_children(swr_get_class(), AV_OPT_FLAG_AUDIO_PARAM);
      show_help_children(avfilter_get_class(), AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM);
   }
}

void show_usage(void)
{
   av_log(NULL, AV_LOG_INFO, "Hyper fast Audio and Video encoder\n");
   av_log(NULL, AV_LOG_INFO, "usage: %s [options] [[infile options] -i infile]... {[outfile options] outfile}...\n", program_name);
   av_log(NULL, AV_LOG_INFO, "\n");
}

static const OptionGroupDefs groups = {{"output url", NULL, OPT_OUTPUT}, {"input url", "i", OPT_INPUT}};

void ffmpeg_parse_options(int argc, char **argv)
{
   uint8_t error[128];
   int ret;

   /* perform system-dependent conversions for arguments list */
   prepare_app_arguments(&argc, &argv);

   // new option parsing context
   OptionParseContext octx(groups); // groups (global) accepted list of option groups

   /* split the commandline into an internal representation */
   octx.split_commandline(argc, argv, options) < 0); // options (global) list of all options

   /* apply global options */
   ret = parse_optgroup(NULL, &octx.global_opts);
   if (ret < 0)
   {
      av_log(NULL, AV_LOG_FATAL, "Error parsing global options: ");
      goto fail;
   }

   /* configure terminal and setup signal handlers */
   // term_init();

   // process all input file groups
   //ret = open_files(&octx.groups[GROUP_INFILE], "input", open_input_file);
   InputFiles input_files;
   for (auto group = octx.groups.begin(); group < octx.groups.end(); group++)
   {
      if (groups->flags&OPT_INPUT)
      {
         InputOptionContext in_opts(options, OPT_INPUT);
         in_opts.parse(*group);
         input_files.emplace_back(group->arg, input_files.size(), in_opts);
      }
   }

   /* create the complex filtergraphs */
   ret = init_complex_filters();
   if (ret < 0)
   {
      av_log(NULL, AV_LOG_FATAL, "Error initializing complex filters.\n");
      goto fail;
   }

   /* open output files */
   // ret = open_files(&octx.groups[GROUP_OUTFILE], "output", open_output_file);
   OutputFiles output_files;
   for (auto group = octx.groups.begin(); group < octx.groups.end(); group++)
   {
      if (groups->flags&OPT_OUTPUT)
      {
         OutputOptionContext in_opts(options, OPT_INPUT);
         out_opts.parse(*group);
         output_files.emplace_back(group->arg, output_files.size(), out_opts);
      }
   }

   /* configure the complex filtergraphs */
   ret = configure_complex_filters();
   if (ret < 0)
   {
      av_log(NULL, AV_LOG_FATAL, "Error configuring complex filters.\n");
      goto fail;
   }
}

static int opt_progress(void *optctx, const char *opt, const char *arg)
{
   AVIOContext *avio = NULL;
   int ret;

   if (!strcmp(arg, "-"))
      arg = "pipe:";
   ret = avio_open2(&avio, arg, AVIO_FLAG_WRITE, &int_cb, NULL);
   if (ret < 0)
   {
      av_log(NULL, AV_LOG_ERROR, "Failed to open progress URL \"%s\": %s\n",
             arg, av_err2str(ret));
      return ret;
   }
   progress_avio = avio;
   return 0;
}
