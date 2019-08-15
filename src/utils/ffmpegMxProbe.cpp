#include "ffmpegMxProbe.h"

#include <algorithm>
#include <set>

extern "C"
{
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include "ffmpegException.h"
#include "ffmpeg_utils.h"
#include "mxutils.h"

using namespace ffmpeg;

void MxProbe::close()
{
  if (!fmt_ctx)
  {
    std::for_each(st_dec_ctx.begin(), st_dec_ctx.end(),
                  [](auto ctx) { av_codec_context_delete(ctx); });
    st_dec_ctx.clear();             // kill stream codecs first
    avformat_close_input(&fmt_ctx); // close file and clear fmt_ctx
  }
}

void MxProbe::open(const char *infile, AVInputFormat *iformat,
                         AVDictionary *opts)
{
  int err, i;
  int scan_all_pmts_set = 0;

  fmt_ctx = avformat_alloc_context();
  if (!fmt_ctx)
    throw Exception("%s: Could not allocate memory for format context.", infile);

  // if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE))
  // {
  //     av_dict_set(&format_opts, "scan_all_pmts", "1",
  //     AV_DICT_DONT_OVERWRITE); scan_all_pmts_set = 1;
  // }
  if ((err = avformat_open_input(&fmt_ctx, infile, iformat,
                                 opts ? &opts : nullptr)) < 0)
  {
    // try search in the MATLAB path before quitting
    std::string filepath = mxWhich(infile);
    if (filepath.size())
      err = avformat_open_input(&fmt_ctx, filepath.c_str(), iformat,
                                opts ? &opts : nullptr);
    if (err < 0) // no luck
      throw Exception(err);
  }

  // fmt_ctx valid

  // fill stream information if not populated yet
  err = avformat_find_stream_info(fmt_ctx, opts ? &opts : nullptr);
  if (err < 0) throw Exception(err);

  // if (scan_all_pmts_set)
  //     av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
  // if ((t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX)))
  // {
  //     av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
  //     return AVERROR_OPTION_NOT_FOUND;
  // }

  /* bind a decoder to each input stream */
  for (i = 0; i < (int)fmt_ctx->nb_streams; i++)
    st_dec_ctx.push_back(open_stream(fmt_ctx->streams[i], opts));

  // save the file name
  filename = infile;
}

AVCodecContext *MxProbe::open_stream(AVStream *st, AVDictionary *opts)
{
  AVCodecContext *dec_ctx = nullptr;
  AVCodec *codec;

  if (st->codecpar->codec_id == AV_CODEC_ID_PROBE)
  {
    Exception::log(AV_LOG_WARNING,
                     "Failed to probe codec for input stream %d\n", st->index);
    return dec_ctx;
  }

  codec = avcodec_find_decoder(st->codecpar->codec_id);
  if (!codec)
  {
    Exception::log(AV_LOG_WARNING,
                     "Unsupported codec with id %d for input stream %d\n",
                     st->codecpar->codec_id, st->index);
    return dec_ctx;
  }

  AVDictionary *codec_opts =
      filter_codec_opts(opts, st->codecpar->codec_id, fmt_ctx, st, codec);
  AVDictionaryAutoDelete(codec_opts);

  dec_ctx = avcodec_alloc_context3(codec);
  if (!dec_ctx) throw Exception(AVERROR(ENOMEM));
  AVCodecContextAutoDelete(dec_ctx);

  int err = avcodec_parameters_to_context(dec_ctx, st->codecpar);
  if (err < 0) throw Exception(err);

  dec_ctx->pkt_timebase = st->time_base;
  dec_ctx->framerate = st->avg_frame_rate;

  if (avcodec_open2(dec_ctx, codec, &codec_opts) < 0)
  {
    Exception::log(AV_LOG_WARNING,
                     "Could not open codec for input stream %d\n", st->index);
    return dec_ctx;
  }

  AVDictionaryEntry *t;
  while ((t = av_dict_get(codec_opts, "", NULL, AV_DICT_IGNORE_SUFFIX)))
  {
    Exception::log(AV_LOG_ERROR, "Option %s for input stream %d not found\n",
                     t->key, st->index);
  }

  return dec_ctx;
}

std::vector<std::string> MxProbe::getMediaTypes() const
{
  // create a unique list of codec types
  std::set<AVMediaType> types;
  for (int i = 0; i < (int)fmt_ctx->nb_streams; ++i)
    types.insert(fmt_ctx->streams[i]->codecpar->codec_type);

  // convert enum to string
  std::vector<std::string> ret;
  for (auto it = types.begin(); it != types.end(); ++it)
    ret.emplace_back(av_get_media_type_string(*it));
  return ret;
}

double MxProbe::getDuration() const
{
  if (!fmt_ctx) throw Exception("No file is open.");

  int64_t duration =
      fmt_ctx->duration + (fmt_ctx->duration <= INT64_MAX - 5000 ? 5000 : 0);
  return duration / (double)AV_TIME_BASE;
}

int MxProbe::getStreamIndex(const enum AVMediaType type,
                                  int wanted_stream_index) const
{
  if (!fmt_ctx) throw Exception( "No file is open.\n");
  return av_find_best_stream(fmt_ctx, type, wanted_stream_index, -1, nullptr,
                             0);
}

int MxProbe::getStreamIndex(const std::string &spec_str) const
{
  if (!fmt_ctx) throw Exception("No file is open.");
  const char *spec = spec_str.c_str();

  if (!fmt_ctx->nb_streams) return AVERROR_STREAM_NOT_FOUND;

  for (int i = 0; i < (int)fmt_ctx->nb_streams; ++i)
    if (avformat_match_stream_specifier(fmt_ctx, fmt_ctx->streams[i], spec) > 0)
      return i;

  return AVERROR_STREAM_NOT_FOUND;
}

double MxProbe::getVideoFrameRate(int wanted_stream_index,
                                        const bool get_avg) const
{
  int i = getStreamIndex(AVMEDIA_TYPE_VIDEO, wanted_stream_index);
  if (i < 0) throw Exception( "No video stream found.\n");
  return av_q2d(get_avg ? (fmt_ctx->streams[i]->avg_frame_rate)
                        : (fmt_ctx->streams[i]->r_frame_rate));
}

double MxProbe::getVideoFrameRate(const std::string &spec_str,
                                        const bool get_avg) const
{
  int i = getStreamIndex(spec_str);
  if (i < 0 || fmt_ctx->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
    throw Exception(
                     "Stream specifier \"%s\" is either invalid expression or "
                     "no match found.\n",
                     spec_str.c_str());
  return av_q2d(get_avg ? (fmt_ctx->streams[i]->avg_frame_rate)
                        : (fmt_ctx->streams[i]->r_frame_rate));
}

int MxProbe::getAudioSampleRate(int wanted_stream_index) const
{
  int i = getStreamIndex(AVMEDIA_TYPE_AUDIO, wanted_stream_index);
  if (i < 0) throw Exception( "No audio stream found.\n");
  return fmt_ctx->streams[i]->codecpar->sample_rate;
}

int MxProbe::getAudioSampleRate(const std::string &spec_str) const
{
  int i = getStreamIndex(spec_str);
  if (i < 0 || fmt_ctx->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
    throw Exception(
                     "Stream specifier \"%s\" is either invalid expression or "
                     "no match found.\n",
                     spec_str.c_str());

  return fmt_ctx->streams[i]->codecpar->sample_rate;
}

////////////////////////////////////////////////////////////////////////////

void MxProbe::dumpToMatlab(mxArray *mxInfo, const int index) const
{
  if (!fmt_ctx) throw Exception( "No file is open.\n");
  ///////////////////////////////////////////
  // MACROs to set mxArray struct fields
  mxArray *mxTMP;

#define mxSetEmptyField(fname)                                                 \
  mxSetField(mxInfo, index, (fname), mxCreateDoubleMatrix(0, 0, mxREAL))
#define mxSetScalarField(fname, fval)                                          \
  mxSetField(mxInfo, index, (fname), mxCreateDoubleScalar((double)(fval)))
#define mxSetInt64ScalarField(fname, fval)                                     \
  {                                                                            \
    mxTMP = mxCreateNumericMatrix(1, 1, mxINT64_CLASS, mxREAL);                \
    *(int64_t *)mxGetData(mxTMP) = fval;                                       \
    mxSetField(mxInfo, index, (fname), mxTMP);                                 \
  }
#define mxSetStringField(fname, fval)                                          \
  mxSetField(mxInfo, index, (fname), mxCreateString((fval)))
#define mxSetRatioField(fname, fval)                                           \
  mxTMP = mxCreateDoubleMatrix(1, 2, mxREAL);                                  \
  pr = mxGetPr(mxTMP);                                                         \
  pr[0] = (fval).num;                                                          \
  pr[1] = (fval).den;                                                          \
  mxSetField(mxInfo, index, (fname), mxTMP)

  mxSetStringField("format", fmt_ctx->iformat->name);
  mxSetStringField("filename", filename.c_str());
  mxSetField(mxInfo, index, "metadata", mxCreateTags(fmt_ctx->metadata));

  if (fmt_ctx->duration != AV_NOPTS_VALUE)
  {
    int64_t duration =
        fmt_ctx->duration + (fmt_ctx->duration <= INT64_MAX - 5000 ? 5000 : 0);
    mxSetInt64ScalarField("duration_ts", duration);
    mxSetScalarField("duration", duration / (double)AV_TIME_BASE);
  }
  else
  {
    mxSetStringField("duration_ts", "N/A");
    mxSetStringField("duration", "N/A");
  }
  if (fmt_ctx->start_time != AV_NOPTS_VALUE)
  {
    mxSetInt64ScalarField("start_ts", fmt_ctx->start_time);
    mxSetScalarField("start", fmt_ctx->start_time / (double)AV_TIME_BASE);
  }
  else
  {
    mxSetEmptyField("start_ts");
    mxSetEmptyField("start");
  }
  if (fmt_ctx->bit_rate) { mxSetScalarField("bitrate", fmt_ctx->bit_rate); }
  else
  {
    mxSetStringField("bitrate", "N/A");
  }

  mxArray *mxChapters = createMxChapterStruct(fmt_ctx->nb_chapters);
  mxSetField(mxInfo, index, "chapters", mxChapters);
  for (int i = 0; i < (int)fmt_ctx->nb_chapters; i++)
  {
    AVChapter *ch = fmt_ctx->chapters[i];
    mxSetField(mxChapters, i, "start",
               mxCreateDoubleScalar(ch->start * av_q2d(ch->time_base)));
    mxSetField(mxChapters, i, "end",
               mxCreateDoubleScalar(ch->end * av_q2d(ch->time_base)));
    mxSetField(mxChapters, i, "metadata", mxCreateTags(ch->metadata));
  }

  std::vector<bool> notshown(fmt_ctx->nb_streams, true);
  int total = 0; // total streams in programs

  mxArray *mxPrograms = createMxProgramStruct(fmt_ctx->nb_programs);
  mxSetField(mxInfo, index, "programs", mxPrograms);
  if (fmt_ctx->nb_programs)
  {
    int j, k;
    for (j = 0; j < (int)fmt_ctx->nb_programs; j++)
    {
      AVDictionaryEntry *name =
          av_dict_get(fmt_ctx->programs[j]->metadata, "name", NULL, 0);

      mxSetField(mxPrograms, j, "id",
                 mxCreateDoubleScalar(fmt_ctx->programs[j]->id));
      mxSetField(mxPrograms, j, "name",
                 mxCreateString(name ? name->value : ""));
      mxSetField(mxPrograms, j, "metadata",
                 mxCreateTags(fmt_ctx->programs[j]->metadata));

      mxArray *mxStreams =
          createMxStreamStruct(fmt_ctx->programs[j]->nb_stream_indexes);
      for (k = 0; k < (int)fmt_ctx->programs[j]->nb_stream_indexes; k++)
      {
        dump_stream_to_matlab(fmt_ctx->programs[j]->stream_index[k], mxStreams,
                              k);
        notshown[fmt_ctx->programs[j]->stream_index[k]] = false;
      }
      total += fmt_ctx->programs[j]->nb_stream_indexes;
    }
  }

  mxArray *mxStreams = createMxStreamStruct(fmt_ctx->nb_streams - total);
  mxSetField(mxInfo, index, "streams", mxStreams);
  int j = 0;
  for (int i = 0; i < (int)fmt_ctx->nb_streams; i++)
    if (notshown[i]) { dump_stream_to_matlab(i, mxStreams, j++); }
}

void MxProbe::dump_stream_to_matlab(const int sid, mxArray *mxInfo,
                                          const int index) const
{
  AVStream *st = fmt_ctx->streams[sid];
  AVCodecContext *dec_ctx = st_dec_ctx[sid];

#define BUF_SIZE 128
  char strbuf[BUF_SIZE];
  const char *s;
  const AVCodecDescriptor *cd;
  int ret = 0;
  const char *profile = NULL;

  mxArray *mxTMP;
  double *pr;

  ///////////////////////////////////////////
  // MACROs to set mxArray struct fields
#undef mxSetScalarField
#define mxSetScalarField(fname, fval)                                          \
  mxSetField(mxInfo, index, (fname), mxCreateDoubleScalar((fval)))
#define mxSetStringField(fname, fval)                                          \
  mxSetField(mxInfo, index, (fname), mxCreateString((fval)))
#define mxSetRatioField(fname, fval)                                           \
  mxTMP = mxCreateDoubleMatrix(1, 2, mxREAL);                                  \
  pr = mxGetPr(mxTMP);                                                         \
  pr[0] = (fval).num;                                                          \
  pr[1] = (fval).den;                                                          \
  mxSetField(mxInfo, index, (fname), mxTMP)
#define mxSetColorRangeField(fname, fval)                                      \
  s = av_color_range_name(fval);                                               \
  mxSetStringField(fname,                                                      \
                   s && (fval != AVCOL_RANGE_UNSPECIFIED) ? s : "unknown")
#define mxSetColorSpaceField(fname, fval)                                      \
  s = av_color_space_name(fval);                                               \
  mxSetStringField(fname, s && (fval != AVCOL_SPC_UNSPECIFIED) ? s : "unknown")
#define mxSetColorPrimariesField(fname, fval)                                  \
  s = av_color_primaries_name(fval);                                           \
  mxSetStringField(fname, s && (fval != AVCOL_PRI_UNSPECIFIED) ? s : "unknown")
#define mxSetColorTransferField(fname, fval)                                   \
  s = av_color_transfer_name(fval);                                            \
  mxSetStringField(fname, s && (fval != AVCOL_TRC_UNSPECIFIED) ? s : "unknown")
#define mxSetChromaLocationField(fname, fval)                                  \
  s = av_chroma_location_name(fval);                                           \
  mxSetStringField(                                                            \
      fname, s && (fval != AVCHROMA_LOC_UNSPECIFIED) ? s : "unspecified")
#define mxSetTimestampField(fname, fval, is_duration)                          \
  if ((!is_duration && fval == AV_NOPTS_VALUE) || (is_duration && fval == 0))  \
    mxSetStringField(fname, "N/A");                                            \
  else                                                                         \
  {                                                                            \
    mxTMP = mxCreateNumericMatrix(1, 1, mxINT64_CLASS, mxREAL);                \
    *(int64_t *)mxGetData(mxTMP) = fval;                                       \
    mxSetField(mxInfo, index, fname, mxTMP);                                   \
  }
#define mxSetTimeField(fname, fval, is_duration)                               \
  if ((!is_duration && fval == AV_NOPTS_VALUE) || (is_duration && fval == 0))  \
    mxSetStringField(fname, "N/A");                                            \
  else                                                                         \
    mxSetScalarField(fname, fval *av_q2d(st->time_base))

  ///////////////////////////////////////////

  mxSetScalarField("index", st->index);

  AVCodecParameters *par = st->codecpar;
  cd = avcodec_descriptor_get(par->codec_id);
  mxSetStringField("codec_name", cd ? cd->name : "unknown");
  mxSetStringField("codec_long_name",
                   (cd && cd->long_name) ? cd->long_name : "unknown");

  if (profile = avcodec_profile_name(par->codec_id, par->profile))
  { mxSetStringField("profile", profile); } else
  {
    if (par->profile != FF_PROFILE_UNKNOWN)
    {
      char profile_num[12];
      snprintf(profile_num, sizeof(profile_num), "%d", par->profile);
      mxSetStringField("profile", profile_num);
    }
    else
    {
      mxSetStringField("profile", "unknown");
    }
  }

  s = av_get_media_type_string(par->codec_type);
  if (s) { mxSetStringField("codec_type", s); }
  else
  {
    mxSetStringField("codec_type", "unknown");
  }

  /* print AVI/FourCC tag */
  char fourcc[AV_FOURCC_MAX_STRING_SIZE];
  mxSetStringField("codec_tag_string",
                   av_fourcc_make_string(fourcc, par->codec_tag));
  mxSetScalarField("codec_tag", par->codec_tag);

  switch (par->codec_type)
  {
  case AVMEDIA_TYPE_VIDEO:
    mxSetScalarField("width", par->width);
    mxSetScalarField("height", par->height);
    mxSetScalarField("has_b_frames", par->video_delay);
    AVRational sar;
    sar = av_guess_sample_aspect_ratio(fmt_ctx, st, NULL);
    if (sar.num)
    {
      mxSetRatioField("sample_aspect_ratio", sar);

      AVRational dar;
      av_reduce(&dar.num, &dar.den, par->width * sar.num, par->height * sar.den,
                1024 * 1024);
      mxSetRatioField("display_aspect_ratio", dar);
    }
    else
    {
      mxSetStringField("sample_aspect_ratio", "N/A");
      mxSetStringField("display_aspect_ratio", "N/A");
    }
    s = av_get_pix_fmt_name((AVPixelFormat)par->format);
    mxSetStringField("pix_fmt", s ? s : "unknown");

    mxSetScalarField("level", par->level);

    mxSetColorRangeField("color_range", par->color_range);
    mxSetColorSpaceField("color_space", par->color_space);
    mxSetColorPrimariesField("color_primaries", par->color_primaries);
    mxSetColorTransferField("color_transfer", par->color_trc);
    mxSetChromaLocationField("chroma_location", par->chroma_location);

    mxSetStringField("field_order",
                     (par->field_order == AV_FIELD_PROGRESSIVE)
                         ? "progressive"
                         : (par->field_order == AV_FIELD_TT)
                               ? "tt"
                               : (par->field_order == AV_FIELD_BB)
                                     ? "bb"
                                     : (par->field_order == AV_FIELD_TB)
                                           ? "tb"
                                           : (par->field_order == AV_FIELD_BT)
                                                 ? "bt"
                                                 : "unknown");

    if (dec_ctx) { mxSetScalarField("refs", dec_ctx->refs); }
    break;

  case AVMEDIA_TYPE_AUDIO:
    s = av_get_sample_fmt_name((AVSampleFormat)par->format);
    mxSetStringField("sample_fmt", s ? s : "unknown");
    mxSetScalarField("sample_rate", par->sample_rate);
    mxSetScalarField("channels", par->channels);

    if (par->channel_layout)
    {
      av_get_channel_layout_string(strbuf, BUF_SIZE, par->channels,
                                   par->channel_layout);
      mxSetStringField("channel_layout", strbuf);
    }
    else
    {
      mxSetStringField("channel_layout", "unknown");
    }

    mxSetScalarField("bits_per_sample", av_get_bits_per_sample(par->codec_id));
    break;

  case AVMEDIA_TYPE_SUBTITLE:
    if (par->width) { mxSetScalarField("width", par->width); }
    else
    {
      mxSetStringField("width", "N/A");
    }
    if (par->height) { mxSetScalarField("height", par->height); }
    else
    {
      mxSetStringField("height", "N/A");
    }
    break;
  }

  if (fmt_ctx->iformat->flags) { mxSetScalarField("id", st->id); }
  else
  {
    mxSetStringField("id", "N/A");
  }
  mxSetRatioField("r_frame_rate", st->r_frame_rate);
  mxSetRatioField("avg_frame_rate", st->avg_frame_rate);
  mxSetRatioField("time_base", st->time_base);
  mxSetTimestampField("start_pts", st->start_time, false);
  mxSetTimeField("start_time", st->start_time, false);
  mxSetTimestampField("duration_ts", st->duration, true);
  mxSetTimeField("duration", st->duration, true);
  if (par->bit_rate > 0)
  { mxSetScalarField("bit_rate", (double)par->bit_rate); } else
  {
    mxSetStringField("bit_rate", "N/A");
  }
  if (dec_ctx && dec_ctx->bits_per_raw_sample > 0)
  { mxSetScalarField("bits_per_raw_sample", dec_ctx->bits_per_raw_sample); }
  else
  {
    mxSetStringField("bits_per_raw_sample", "N/A");
  }
  if (st->nb_frames) { mxSetScalarField("nb_frames", (double)st->nb_frames); }
  else
  {
    mxSetStringField("nb_frames", "N/A");
  }

  /* Get disposition information */
  std::vector<std::string> dispositions;

#define QUEUE_DISPOSITION(flagname, name)                                      \
  if (st->disposition & AV_DISPOSITION_##flagname) dispositions.push_back(name)
  QUEUE_DISPOSITION(DEFAULT, "default");
  QUEUE_DISPOSITION(DUB, "dub");
  QUEUE_DISPOSITION(ORIGINAL, "original");
  QUEUE_DISPOSITION(COMMENT, "comment");
  QUEUE_DISPOSITION(LYRICS, "lyrics");
  QUEUE_DISPOSITION(KARAOKE, "karaoke");
  QUEUE_DISPOSITION(FORCED, "forced");
  QUEUE_DISPOSITION(HEARING_IMPAIRED, "hearing_impaired");
  QUEUE_DISPOSITION(VISUAL_IMPAIRED, "visual_impaired");
  QUEUE_DISPOSITION(CLEAN_EFFECTS, "clean_effects");
  QUEUE_DISPOSITION(ATTACHED_PIC, "attached_pic");
  QUEUE_DISPOSITION(TIMED_THUMBNAILS, "timed_thumbnails");
  QUEUE_DISPOSITION(CAPTIONS, "captions");
  QUEUE_DISPOSITION(DESCRIPTIONS, "descriptions");
  QUEUE_DISPOSITION(METADATA, "metadata");
  QUEUE_DISPOSITION(DEPENDENT, "dependent");
  QUEUE_DISPOSITION(STILL_IMAGE, "still_image");

  mxArray *mxCell = mxCreateCellMatrix(1, dispositions.size());
  mxSetField(mxInfo, index, "dispositions", mxCell);
  for (int i = 0; i < dispositions.size(); ++i)
    mxSetCell(mxCell, i, mxCreateString(dispositions[i].c_str()));

  mxSetField(mxInfo, index, "metadata", mxCreateTags(st->metadata));
}

#define ARRAY_LENGTH(_array_) (sizeof(_array_) / sizeof(_array_[0]))

mxArray *MxProbe::createMxInfoStruct(mwSize size)
{
  return mxCreateStructMatrix(size, 1, ARRAY_LENGTH(field_names), field_names);
}

mxArray *MxProbe::createMxChapterStruct(mwSize size)
{
  return mxCreateStructMatrix(size, 1, ARRAY_LENGTH(chapter_field_names),
                              chapter_field_names);
}

mxArray *MxProbe::createMxProgramStruct(mwSize size)
{
  return mxCreateStructMatrix(size, 1, ARRAY_LENGTH(program_field_names),
                              program_field_names);
}

mxArray *MxProbe::createMxStreamStruct(mwSize size)
{
  return mxCreateStructMatrix(size, 1, ARRAY_LENGTH(stream_field_names),
                              stream_field_names);
}

const char *MxProbe::field_names[] = {
    "format",  "filename", "metadata", "duration_ts", "duration", "start_ts",
    "bitrate", "start",    "streams",  "chapters",    "programs"};

const char *MxProbe::chapter_field_names[] = {"start", "end", "metadata"};

const char *MxProbe::program_field_names[] = {"id", "name", "metadata",
                                                    "streams"};

const char *MxProbe::stream_field_names[] = {"index",
                                                   "codec_name",
                                                   "codec_long_name",
                                                   "profile",
                                                   "codec_type",
                                                   "codec_tag_string",
                                                   "codec_tag",
                                                   "width", // video
                                                   "height",
                                                   "has_b_frames",
                                                   "sample_aspect_ratio",
                                                   "display_aspect_ratio",
                                                   "pix_fmt",
                                                   "level",
                                                   "color_range",
                                                   "color_space",
                                                   "color_transfer",
                                                   "color_primaries",
                                                   "chroma_location",
                                                   "field_order",
                                                   "refs",
                                                   "sample_fmt", // audio
                                                   "sample_rate",
                                                   "channel_layout",
                                                   "bits_per_sample", // end
                                                   "id",
                                                   "r_frame_rate",
                                                   "avg_frame_rate",
                                                   "time_base",
                                                   "start_pts",
                                                   "start_time",
                                                   "duration_ts",
                                                   "duration",
                                                   "bit_rate",
                                                   "bits_per_raw_sample",
                                                   "nb_frames",
                                                   "dispositions",
                                                   "metadata"};
