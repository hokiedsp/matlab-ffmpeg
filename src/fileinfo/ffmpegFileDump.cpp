#include "ffmpegFileDump.h"

#include "../Common/ffmpegException.h"

#include <algorithm>

#include "../Common/ffmpegPtrs.h"

extern "C" {
  #include <libavutil/avutil.h>
  #include <libavutil/pixdesc.h>
}

#include <mex.h> // for debugging

using namespace ffmpeg;

FileDump::FileDump(const std::string &filename) : Duration(NAN), StartTime(NAN), BitRate(-1), ic(NULL)
{
  // create new file format context
  open_file(filename);
  URL = filename;

  // start decoding
  dump_format();
}

FileDump::~FileDump()
{
  if (ic)
    avformat_close_input(&ic);
}

// based on ffmpeg_opt.c/open_input_file()
void FileDump::open_file(const std::string &filename)
{
  if (filename.empty())
    throw ffmpegException("filename must be non-empty.");

  /* get default parameters from command line */
  ic = avformat_alloc_context(); // file format context
  if (!ic)
    throw ffmpegException(filename, ENOMEM);

  ic->flags |= AVFMT_FLAG_NONBLOCK;
  ic->interrupt_callback = {NULL, NULL}; // from ffmpegBase

  ////////////////////

  AVDictionary *d = NULL;
  av_dict_set(&d, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);

  /* open the input file with generic avformat function */
  int err;
  if ((err = avformat_open_input(&ic, filename.c_str(), NULL, &d)) < 0)
    throw ffmpegException(filename, err);

  if (d)
    av_dict_free(&d);

  /* If not enough info to get the stream parameters, we decode the
       first frames to get it. (used in mpeg case for example) */
  if (avformat_find_stream_info(ic, NULL) < 0)
    throw ffmpegException("Could not find codec parameters");
}

// based on libavformat/dump.c/av_dump_format()
void FileDump::dump_format()
{
  Format = ic->iformat->name;
  MetaData = dump_metadata(ic->metadata);

  if (ic->duration != AV_NOPTS_VALUE)
  {
    int64_t duration = ic->duration + (ic->duration <= INT64_MAX - 5000 ? 5000 : 0);
    Duration = double(duration / 100) / (AV_TIME_BASE / 100);
  }
  else
  {
    Duration = NAN;
  }

  if (ic->start_time != AV_NOPTS_VALUE)
    StartTime = double(ic->start_time / 100) / (AV_TIME_BASE / 100);
  else
    StartTime = NAN;

  if (ic->bit_rate)
    BitRate = ic->bit_rate; // bit/s
  else
    BitRate = -1;

  // Retrieving chapters
  Chapters.resize(ic->nb_chapters);
  for (unsigned int i = 0; i < ic->nb_chapters; i++)
  {
    AVChapter *ch = ic->chapters[i];
    Chapters[i].StartTime = ch->start * av_q2d(ch->time_base);
    Chapters[i].EndTime = ch->end * av_q2d(ch->time_base);
    Chapters[i].MetaData = dump_metadata(ch->metadata);
  }

  // Retrieving Programs
  if (ic->nb_programs)
  {
    Programs.resize(ic->nb_programs);
    for (unsigned int j = 0; j < ic->nb_programs; j++)
    {
      AVProgram *prog = ic->programs[j];
      Programs[j].ID = prog->id;
      AVDictionaryEntry *name = av_dict_get(prog->metadata, "name", NULL, 0);
      if (name)
        Programs[j].Name = name->value;
      Programs[j].MetaData = dump_metadata(prog->metadata);
      Programs[j].StreamIndices.resize(prog->nb_stream_indexes);
      std::copy_n(prog->stream_index, prog->nb_stream_indexes, Programs[j].StreamIndices.begin());
    }
  }

  Streams.resize(ic->nb_streams);
  for (unsigned int i = 0; i < ic->nb_streams; i++)
  {
    // dump codex context data
    Streams[i] = dump_stream_format(ic->streams[i]);
  }
}

// based on libavformat/dump.c/dump_metadata()
FileDump::MetaData_t FileDump::dump_metadata(AVDictionary *m)
{
  FileDump::MetaData_t info;

  if (m && !(av_dict_count(m) == 1 && av_dict_get(m, "language", NULL, 0)))
  {
    AVDictionaryEntry *tag = NULL;

    while ((tag = av_dict_get(m, "", tag, AV_DICT_IGNORE_SUFFIX)))
      if (strcmp("language", tag->key))
        info.emplace_back(tag->key, tag->value);
  }

  return info;
}

// based on libavcodec/utils.c/avcodec_string()
FileDump::Stream_s FileDump::dump_codec(AVStream *st)
{
  FileDump::Stream_s codec;
  const char *strval;

  AVCodecContext *enc = avcodec_alloc_context3(NULL);
  if (!enc)
    throw ffmpegException("Failed to allocate memory for codec context.");
  CodecCtxPtr dec_ctx(enc, delete_codec_ctx); // use unique_ptr to automatically delete

  if (avcodec_parameters_to_context(enc, st->codecpar) < 0)
    throw ffmpegException("Failed to get codec context from parameters.");

  /////////////////////////////////////////////////////////////////////////////////////////////////

  //avcodec_string(buf, sizeof(buf), enc, false);
  if (strval = av_get_media_type_string(enc->codec_type))
    codec.Type = strval;
  else
    codec.Type = "unknown";

  if (strval = avcodec_get_name(enc->codec_id))
  {
    codec.CodecName = strval;
    if (enc->codec && strcmp(enc->codec->name, strval))
    {
      codec.CodecName += " (";
      codec.CodecName += enc->codec->name;
      codec.CodecName += ")";
    }
  }

  if (strval = avcodec_profile_name(enc->codec_id, enc->profile))
    codec.CodecProfile = strval;

  if (enc->codec_tag)
  {
#ifdef AV_FOURCC_MAX_STRING_SIZE
    char tag_buf[AV_FOURCC_MAX_STRING_SIZE];
    av_fourcc_make_string(tag_buf, enc->codec_tag);
#else
    char tag_buf[32];
    av_get_codec_tag_string(tag_buf, sizeof(tag_buf), enc->codec_tag);
#endif
    codec.CodecTag = tag_buf;
  }

  switch (enc->codec_type)
  {
  case AVMEDIA_TYPE_VIDEO:

    if (enc->refs)
      codec.ReferenceFrames = enc->refs;

    codec.PixelFormat = enc->pix_fmt == AV_PIX_FMT_NONE ? "none" : av_get_pix_fmt_name(enc->pix_fmt);

    if (enc->bits_per_raw_sample && enc->pix_fmt != AV_PIX_FMT_NONE &&
        enc->bits_per_raw_sample < av_pix_fmt_desc_get(enc->pix_fmt)->comp[0].depth)
      codec.BitsPerRawSample = enc->bits_per_raw_sample;

    if (enc->color_range != AVCOL_RANGE_UNSPECIFIED)
      codec.ColorRange = av_color_range_name(enc->color_range);

    if (enc->colorspace != AVCOL_SPC_UNSPECIFIED ||
        enc->color_primaries != AVCOL_PRI_UNSPECIFIED ||
        enc->color_trc != AVCOL_TRC_UNSPECIFIED)
    {
      codec.ColorSpace = av_color_space_name(enc->colorspace);
      if (enc->colorspace != (int)enc->color_primaries ||
          enc->colorspace != (int)enc->color_trc)
      {
        codec.ColorPrimaries = av_color_primaries_name(enc->color_primaries);
        codec.ColorTransfer = av_color_transfer_name(enc->color_trc);
      }
    }

    if (enc->field_order != AV_FIELD_UNKNOWN)
    {
      if (enc->field_order == AV_FIELD_TT)
        codec.FieldOrder = "top first";
      else if (enc->field_order == AV_FIELD_BB)
        codec.FieldOrder = "bottom first";
      else if (enc->field_order == AV_FIELD_TB)
        codec.FieldOrder = "top coded first (swapped)";
      else if (enc->field_order == AV_FIELD_BT)
        codec.FieldOrder = "bottom coded first (swapped)";
      else
        codec.FieldOrder = "progressive";
    }

    if (enc->chroma_sample_location != AVCHROMA_LOC_UNSPECIFIED)
      codec.ChromaSampleLocation = av_chroma_location_name(enc->chroma_sample_location);

    if (enc->width)
    {
      codec.Width = enc->width;
      codec.Height = enc->height;

      if (enc->width != enc->coded_width || enc->height != enc->coded_height)
      {
        codec.CodedWidth = enc->coded_width;
        codec.CodedHeight = enc->coded_height;
      }

      if (enc->sample_aspect_ratio.num)
      {
        AVRational display_aspect_ratio;
        av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                  enc->width * (int64_t)enc->sample_aspect_ratio.num,
                  enc->height * (int64_t)enc->sample_aspect_ratio.den,
                  1024 * 1024);
        codec.SAR = std::make_pair(enc->sample_aspect_ratio.num, enc->sample_aspect_ratio.den);
        codec.DAR = std::make_pair(display_aspect_ratio.num, display_aspect_ratio.den);
      }
    }

    codec.ClosedCaption = enc->properties & FF_CODEC_PROPERTY_CLOSED_CAPTIONS;
    codec.Lossless = enc->properties & FF_CODEC_PROPERTY_LOSSLESS;

    break;
  case AVMEDIA_TYPE_AUDIO:

    if (enc->sample_rate)
      codec.SampleRate = enc->sample_rate;

    {
      char buf[256];
      av_get_channel_layout_string(buf, 256, enc->channels, enc->channel_layout);
      codec.ChannelLayout = buf;
    }

    if (enc->sample_fmt != AV_SAMPLE_FMT_NONE)
      codec.SampleFormat = av_get_sample_fmt_name(enc->sample_fmt);

    if (enc->bits_per_raw_sample > 0 && enc->bits_per_raw_sample != av_get_bytes_per_sample(enc->sample_fmt) * 8)
      codec.BitsPerRawSample = enc->bits_per_raw_sample;

    if (enc->initial_padding)
      codec.InitialPadding = enc->initial_padding;

    if (enc->trailing_padding)
      codec.TrailingPadding = enc->trailing_padding;

    break;
  case AVMEDIA_TYPE_DATA:
    break;
  case AVMEDIA_TYPE_SUBTITLE:
    if (enc->width)
      codec.Width = enc->width;
    codec.Height = enc->height;
    break;
  default:
    break;
  }

  // based on libavcodec/utils.c/get_bit_rate()
  int bits_per_sample;
  switch (enc->codec_type)
  {
  case AVMEDIA_TYPE_VIDEO:
  case AVMEDIA_TYPE_DATA:
  case AVMEDIA_TYPE_SUBTITLE:
  case AVMEDIA_TYPE_ATTACHMENT:
    codec.BitRate = enc->bit_rate;
    break;
  case AVMEDIA_TYPE_AUDIO:
    bits_per_sample = av_get_bits_per_sample(enc->codec_id);
    codec.BitRate = bits_per_sample ? enc->sample_rate * (int64_t)enc->channels * bits_per_sample : enc->bit_rate;
    break;
  default:
    codec.BitRate = 0;
    break;
  }

  if (enc->rc_max_rate > 0)
    codec.MaximumBitRate = enc->rc_max_rate;

  return codec;
}

// based on libavformat/dump.c/dump_stream_format()
FileDump::Stream_s FileDump::dump_stream_format(AVStream *st)
{
  AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL, 0);
  int flags = ic->iformat->flags;

  // dump codec data
  FileDump::Stream_s info = dump_codec(st);

  /* the pid is an important information, so we display it */
  /* XXX: add a generic system */
  if (flags & AVFMT_SHOW_IDS)
    info.ID = st->id;
  else
    info.ID = -1;

  if (lang)
    info.Language = lang->value;

  if (st->sample_aspect_ratio.num &&
      av_cmp_q(st->sample_aspect_ratio, st->codecpar->sample_aspect_ratio))
  {
    AVRational display_aspect_ratio;
    av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
              st->codecpar->width * (int64_t)st->sample_aspect_ratio.num,
              st->codecpar->height * (int64_t)st->sample_aspect_ratio.den,
              1024 * 1024);
    info.SAR = std::make_pair(st->sample_aspect_ratio.num, st->sample_aspect_ratio.den);
    info.DAR = std::make_pair(display_aspect_ratio.num, display_aspect_ratio.den);
  }

  if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
  {
    if (st->avg_frame_rate.den && st->avg_frame_rate.num)
      info.AverageFrameRate = av_q2d(st->avg_frame_rate);
    else
      info.AverageFrameRate = NAN;

    if (st->r_frame_rate.den && st->r_frame_rate.num)
      info.RealBaseFrameRate = av_q2d(st->r_frame_rate);
    else
      info.RealBaseFrameRate = NAN;

    if (st->time_base.den && st->time_base.num)
      info.TimeBase = 1.0f / av_q2d(st->time_base);
    else
      info.TimeBase = NAN;

    if (st->codec->time_base.den && st->codec->time_base.num)
      info.CodecTimeBase = 1.0f / av_q2d(st->codec->time_base);
  }

  info.Dispositions.Default = st->disposition & AV_DISPOSITION_DEFAULT;
  info.Dispositions.Dub = st->disposition & AV_DISPOSITION_DUB;
  info.Dispositions.Original = st->disposition & AV_DISPOSITION_ORIGINAL;
  info.Dispositions.Comment = st->disposition & AV_DISPOSITION_COMMENT;
  info.Dispositions.Lyrics = st->disposition & AV_DISPOSITION_LYRICS;
  info.Dispositions.Karaoke = st->disposition & AV_DISPOSITION_KARAOKE;
  info.Dispositions.Forced = st->disposition & AV_DISPOSITION_FORCED;
  info.Dispositions.HearingImpaired = st->disposition & AV_DISPOSITION_HEARING_IMPAIRED;
  info.Dispositions.VisualImpaired = st->disposition & AV_DISPOSITION_VISUAL_IMPAIRED;
  info.Dispositions.CleanEffects = st->disposition & AV_DISPOSITION_CLEAN_EFFECTS;

  info.MetaData = dump_metadata(st->metadata);
  info.SideData = dump_sidedata(st);

  return info;
}

// based on libavformat/dump.c/dump_sidedata()
FileDump::SideData_t FileDump::dump_sidedata(AVStream *st)
{
  FileDump::SideData_t info(st->nb_side_data);
  for (int i = 0; i < st->nb_side_data; i++)
  {
    AVPacketSideData sd = st->side_data[i];
    switch (sd.type)
    {
    case AV_PKT_DATA_PALETTE:
      info[i].Type = "palette";
      break;
    case AV_PKT_DATA_NEW_EXTRADATA:
      info[i].Type = "new extradata";
      break;
    case AV_PKT_DATA_PARAM_CHANGE:
      info[i].Type = "paramchange";
      // dump_paramchange(ctx, &sd);
      break;
    case AV_PKT_DATA_H263_MB_INFO:
      info[i].Type = "H.263 macroblock info";
      break;
    case AV_PKT_DATA_REPLAYGAIN:
      info[i].Type = "replaygain";
      // av_log(ctx, AV_LOG_INFO, "replaygain: ");
      // dump_replaygain(ctx, &sd);
      break;
    case AV_PKT_DATA_DISPLAYMATRIX:
      info[i].Type = "displaymatrix";
      // av_log(ctx, AV_LOG_INFO, "displaymatrix: rotation of %.2f degrees",
      //        av_display_rotation_get((int32_t *)sd.data));
      break;
    case AV_PKT_DATA_STEREO3D:
      info[i].Type = "stereo3d";
      // dump_stereo3d(ctx, &sd);
      break;
    case AV_PKT_DATA_AUDIO_SERVICE_TYPE:
      info[i].Type = "audio service type";
      // dump_audioservicetype(ctx, &sd);
      break;
    case AV_PKT_DATA_QUALITY_STATS:
      info[i].Type = "quality factor";
      // av_log(ctx, AV_LOG_INFO, "quality factor: %d, pict_type: %c", AV_RL32(sd.data), av_get_picture_type_char(sd.data[4]));
      break;
    case AV_PKT_DATA_CPB_PROPERTIES:
      info[i].Type = "cpb";
      // dump_cpb(ctx, &sd);
      break;
    case AV_PKT_DATA_MASTERING_DISPLAY_METADATA:
      info[i].Type = "mastering display metadata";
      // dump_mastering_display_metadata(ctx, &sd);
      break;
    default:
      info[i].Type = "unknown side data: ";
      info[i].Type += sd.type;
      // av_log(ctx, AV_LOG_INFO,
      //        "unknown side data type %d (%d bytes)", sd.type, sd.size);
      break;
    }
  }
  return info;
}
