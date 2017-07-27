#include "ffmpegOutputStream.h"

#include <sstream>
#include <cstdlib> // getenv()

using namespace ffmpeg;

OutputStream::OutputStream(OutputFile &f, const int i, AVFormatContext *oc, const AVMediaType type, OptionsContext &o, InputStream *src)
    : file(&f), index(i), encoding_needed(false), stream_copy(false), last_mux_dts(AV_NOPTS_VALUE),
      st(NULL), enc(NULL), enc_ctx(NULL), ref_par(NULL), encoder_opts(NULL), bsf_ctx(NULL), nb_bitstream_filters(0), bsf_extradata_updated(NULL)
{
   int ret;

   // create new AVStream
   st = avformat_new_stream(oc, NULL);
   if (!st)
      throw ffmpegException("Could not alloc stream.");
   st->id = o.streamid_map.back();
   st->codecpar->codec_type = type;

   // set encoding: enc, encoding_needed, stream_copy
   if (!(enc = o.choose_encoder(oc, st, stream_copy, encoding_needed)) && encoding_needed)
   {
      std::ostringstream msg << "Error selecting an encoder for stream " << file->index << ":" << index;
      throw ffmpegException(msg.str());
   }

   // set encoding context
   if (!(enc_ctx = avcodec_alloc_context3(enc)))
      throw ffmpegException("Error allocating the encoding context.");
   enc_ctx->codec_type = type;

   // allocate codec parameter
   if (!(ref_par = avcodec_parameters_alloc()))
      throw ffmpegException("Error allocating the encoding parameters.");

   // set encoder options
   if (enc) // video or audio or subtitle
   {
      encoder_opts = o.filter_codec_opts(enc, oc, st);

      // check for encoder preset option
      std::string preset = o.get_last_preset(oc, st);

      // if preset specified, load it
      if (preset.size())
      {
         AVIOContext *s = NULL;
         if (get_preset_file_2(preset, enc->name, &s) < 0) // grab the preset file content in s
         {
            std::ostringstream msg << "Preset " << preset << " specified for stream " << file->index << ":" << index << ", but could not be opened.";
            throw ffmpegException(msg.str());
         }

         do
         {
            // get next encoder option (one per line)
            char *buf = get_line(s);
            if (!buf[0] || buf[0] == '#')
            {
               av_free(buf);
               continue;
            }

            // get the option value
            if (!(char *arg = strchr(buf, '=')))
               throw ffmpegException("Invalid line found in the preset file.");

            // separate name & value
            *arg++ = 0;

            // set the option
            av_dict_set(&encoder_opts, buf, arg, AV_DICT_DONT_OVERWRITE);
            av_free(buf);
         } while (!s->eof_reached);

         avio_closep(&s);
      }
   }
   else // other codec type
   {
      encoder_opts = o.filter_codec_opts(AV_CODEC_ID_NONE, ic, st);
   }

   // set maximum number of frames to encode
   max_frames = o.get_last_max_frames(oc, st, INT64_MAX);

   // not sure what this option means
   copy_prior_start = o.get_last_copy_prior_start(oc, st, -1);

   // set bitstream filters
   const char *bsfs = o.get_last_bitstream_filters(oc, st);
   while (bsfs && *bsfs)
   {
      const AVBitStreamFilter *filter;
      char *bsf, *bsf_options_str, *bsf_name;

      bsf = av_get_token(&bsfs, ",");
      if (!bsf)
         throw ffmpegException("Invalid bitstream filter specification.");
      bsf_name = av_strtok(bsf, "=", &bsf_options_str);
      if (!bsf_name)
      {
         av_freep(&bsf);
         throw ffmpegException("Invalid bitstream filter specification.");
      }

      filter = av_bsf_get_by_name(bsf_name);
      if (!filter)
      {
         av_freep(&bsf);
         std::ostringstream msg << "Unknown bitstream filter " << bsf_name;
         throw ffmpegException(msg.str());
      }

      bsf_ctx = av_realloc_array(bsf_ctx, nb_bitstream_filters + 1, sizeof(*bsf_ctx));
      if (!bsf_ctx)
      {
         av_freep(&bsf);
         throw ffmpegException("Failed to allocate memory for bitstream filter.");
      }

      ret = av_bsf_alloc(filter, &bsf_ctx[nb_bitstream_filters]);
      if (ret < 0)
      {
         av_freep(&bsf);
         throw ffmpegException("Error allocating a bitstream filter context");
      }

      nb_bitstream_filters++;

      if (bsf_options_str && filter->priv_class)
      {
         const AVOption *opt = av_opt_next(bsf_ctx[nb_bitstream_filters - 1]->priv_data, NULL);
         const char *shorthand[2] = {NULL};

         if (opt)
            shorthand[0] = opt->name;

         ret = av_opt_set_from_string(bsf_ctx[ost->nb_bitstream_filters - 1]->priv_data, bsf_options_str, shorthand, "=", ":");
         if (ret < 0)
         {
            std::ostringstream msg << "Error parsing options for bitstream filter " << bsf_name;
            av_freep(&bsf);
            throw ffmpegException(msg.str());
         }
      }
      av_freep(&bsf);

      if (*bsfs)
         bsfs++;
   }
   if (nb_bitstream_filters)
   {
      bsf_extradata_updated = av_mallocz_array(nb_bitstream_filters, sizeof(*bsf_extradata_updated));
      if (!bsf_extradata_updated)
         throw ffmpegException("Bitstream filter memory allocation failed");
   }

   int ret = 0;

   int i;

   // set codec tag
   std::string codec_tag = o.get_last_codec_tag(oc, st);
   if (codec_tag.size())
   {
      char *next;
      uint32_t tag = codec_tag.stol(&next, 0);
      if (*next)
         tag = AV_RL32(codec_tag.c_str());
      st->codecpar->codec_tag = enc_ctx->codec_tag = tag;
   }

   // set qscale
   if ((double qscale = o.get_last_qscale(oc, st, -1)) >= 0)
   {
      enc_ctx->flags |= AV_CODEC_FLAG_QSCALE;
      enc_ctx->global_quality = FF_QP2LAMBDA * qscale;
   }

   // set disposition (???)
   disposition = o.get_last_disposition(oc, st);

   max_muxing_queue_size = o.get_last_max_muxing_queue_size(oc, st, 128);
   max_muxing_queue_size *= sizeof(AVPacket);

   if (oc->oformat->flags & AVFMT_GLOBALHEADER)
      enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

   // get various output filter configuration dictionaries
   av_dict_copy(&sws_dict, o.g->sws_dict, 0);

   av_dict_copy(&swr_opts, o.g->swr_opts, 0);
   if (enc && av_get_exact_bits_per_sample(enc->id) == 24)
      av_dict_set(&swr_opts, "output_sample_bits", "24", 0);

   av_dict_copy(&resample_opts, o.g->resample_opts, 0);

   // if source stream is given
   if (src >= 0)
   {
      sync_ist = src;
      src->discard = 0;
      src->st->discard = src->user_set_discard;
   }

   // allocate queue for multiplexing
   muxing_queue = av_fifo_alloc(8 * sizeof(AVPacket));
   if (!muxing_queue)
      throw ffmpegException("Failed to allocate memory for muxing_queue.");
}

std::string OutputStream::choose_pix_fmts()
{

   if (AVDictionaryEntry *strict_dict = av_dict_get(encoder_opts, "strict", NULL, 0))
      // used by choose_pixel_fmt() and below
      av_opt_set(enc_ctx, "strict", strict_dict->value, 0);

   if (keep_pix_fmt)
   {
      if (filter)
         avfilter_graph_set_auto_convert(filter->graph->graph, AVFILTER_AUTO_CONVERT_NONE);
      if (enc_ctx->pix_fmt == AV_PIX_FMT_NONE)
         return NULL;
      else
         return av_get_pix_fmt_name(enc_ctx->pix_fmt);
   }

   if (enc_ctx->pix_fmt != AV_PIX_FMT_NONE)
   {
      return av_get_pix_fmt_name(choose_pixel_fmt(st, enc_ctx, enc, enc_ctx->pix_fmt)));
   }
   else if (enc && enc->pix_fmts)
   {
      const AVPixelFormat *p;
      AVIOContext *s = NULL;
      uint8_t *ret;
      int len;

      if (avio_open_dyn_buf(&s) < 0)
         throw ffmpegException("Failed to create dynamic buffer.");

      p = enc->pix_fmts;
      if (enc_ctx->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL)
      {
         p = get_compliance_unofficial_pix_fmts(enc_ctx->codec_id, p);
      }

      for (; *p != AV_PIX_FMT_NONE; p++)
      {
         avio_printf(s, "%s|", av_get_pix_fmt_name(*p));
      }
      len = avio_close_dyn_buf(s, &ret);
      ret[len - 1] = 0;
      std::string rval(ret);
      av_free(ret);
      return rval;
   }
   else
      return "";
}

// static
AVPixelFormat OutputStream::choose_pixel_fmt(AVStream *st, AVCodecContext *enc_ctx, AVCodec *codec, AVPixelFormat target)
{
   if (codec && codec->pix_fmts)
   {
      const AVPixelFormat *p = codec->pix_fmts;
      const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(target);
      int has_alpha = desc ? desc->nb_components % 2 == 0 : 0;
      AVPixelFormat best = AV_PIX_FMT_NONE;

      if (enc_ctx->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL)
      {
         p = get_compliance_unofficial_pix_fmts(enc_ctx->codec_id, p);
      }
      for (; *p != AV_PIX_FMT_NONE; p++)
      {
         best = avcodec_find_best_pix_fmt_of_2(best, *p, target, has_alpha, NULL);
         if (*p == target)
            break;
      }
      if (*p == AV_PIX_FMT_NONE)
      {
         if (target != AV_PIX_FMT_NONE)
            av_log(NULL, AV_LOG_WARNING,
                   "Incompatible pixel format '%s' for codec '%s', auto-selecting format '%s'\n",
                   av_get_pix_fmt_name(target),
                   codec->name,
                   av_get_pix_fmt_name(best));
         return best;
      }
   }
   return target;
}

static const AVPixelFormat *OutputStream::get_compliance_unofficial_pix_fmts(AVCodecID codec_id, const AVPixelFormat default_formats[])
{
   static const AVPixelFormat mjpeg_formats[] =
       {AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_NONE};
   static const AVPixelFormat ljpeg_formats[] =
       {AV_PIX_FMT_BGR24, AV_PIX_FMT_BGRA, AV_PIX_FMT_BGR0,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_NONE};

   if (codec_id == AV_CODEC_ID_MJPEG)
   {
      return mjpeg_formats;
   }
   else if (codec_id == AV_CODEC_ID_LJPEG)
   {
      return ljpeg_formats;
   }
   else
   {
      return default_formats;
   }
}

std::string ffmpegOutputStream::choose_sample_fmts()
{
   if (enc_ctx->sample_fmt != AV_SAMPLE_FMT_NONE)
   {
      return av_get_sample_fmt_name(*p);
   }
   else if (enc && enc->sample_fmts)
   {
      const AVSampleFormat *p;
      AVIOContext *s = NULL;
      uint8_t *ret;
      int len;

      if (avio_open_dyn_buf(&s) < 0)
         throw ffmpegException("Failed to allocate dynamic memory.");

      for (p = enc->sample_fmts; *p != AV_SAMPLE_FMT_NONE; p++)
      {
         avio_printf(s, "%s|", av_get_sample_fmt_name(*p));
      }
      len = avio_close_dyn_buf(s, &ret);
      ret[len - 1] = 0;
      std::string rval(ret);
      av_free(ret);
      return rval;
   }
   else
      return "";
}

std::string ffmpegOutputStream::choose_sample_rates()
{
   if (enc_ctx->sample_rate != 0)
   {
      return std::to_string(enc_ctx->sample_rate);
   }
   else if (enc && enc->supported_samplerates)
   {
      const int *p;
      AVIOContext *s = NULL;
      uint8_t *ret;
      int len;

      if (avio_open_dyn_buf(&s) < 0)
         throw ffmpegException("Failed to allocate dynamic memory.");

      for (p = enc->supported_samplerates; *p != none; p++)
      {
         avio_printf(s, "%s|", std::to_string(rate).c_str());
      }
      len = avio_close_dyn_buf(s, &ret);
      ret[len - 1] = 0;
      std::string rval(ret);
      av_free(ret);
      return rval;
   }
   else
      return "";
}

std::string ffmpegOutputStream::choose_channel_layouts()
{
   if (enc_ctx->channel_layout != 0)
   {
      std::string name("0x");
      name += std::to_string(enc_ctx->channel_layout);
      return name;
   }
   else if (enc && enc->channel_layouts)
   {
      const uint64_t *p;
      AVIOContext *s = NULL;
      uint8_t *ret;
      int len;

      if (avio_open_dyn_buf(&s) < 0)
         throw ffmpegException("Failed to allocate dynamic memory.");

      for (p = enc->channel_layouts; *p != none; p++)
      {
         std::string name("0x");
         name += std::to_string(*p);
         avio_printf(s, "%s|", name.c_str());
      }
      len = avio_close_dyn_buf(s, &ret);
      ret[len - 1] = 0;
      std::string rval(ret);
      av_free(ret);
      return rval;
   }
   else
      return "";
}

int OutputStream::get_preset_file_2(const std::string &preset_name, const std::string &codec_name, AVIOContext *&s)
{
   int ret = -1;
   std::ostringstream filename;
   const std::vector<std::string> base = {getenv("AVCONV_DATADIR"), getenv("HOME"), AVCONV_DATADIR};

   for (auto it = base.begin(); it < base.end() && ret < 0; it++)
   {
      if (it->empty())
         continue;

      if (codec_name)
      {
         filename << *it << i != 1 ? "" : "/.avconv"
                                              << "/" << codec_name << "-" << preset_name << ".avpreset";
         ret = avio_open2(s, filename.str().c_str(), AVIO_FLAG_READ, &ffmpegBase::int_cb, NULL);
      }
      if (ret < 0)
      {
         filename.str("");
         filename << *it << i != 1 ? "" : "/.avconv"
                                              << "/" << preset_name << ".avpreset";
         ret = avio_open2(s, filename.str().c_str(), AVIO_FLAG_READ, &ffmpegBase::int_cb, NULL);
      }
   }
   return ret;
}

uint8_t *OutputStream::get_line(AVIOContext *s)
{
   AVIOContext *line;
   uint8_t *buf;
   char c;

   if (avio_open_dyn_buf(&line) < 0)
      throw ffmpegException("Could not alloc buffer for reading preset.");

   while ((c = avio_r8(s)) && c != '\n')
      avio_w8(line, c);
   avio_w8(line, 0);
   avio_close_dyn_buf(line, &buf);

   return buf;
}

//////////////////////////////////////////////////////

InputStream *OutputStream::get_input_stream(OutputStream *ost)
{
   if (ost->source_index >= 0)
      return input_streams[ost->source_index];
   return NULL;
}

/////////////////////////////////////////////////////////

void parse_forced_key_frames(char *kf, OutputStream *ost,
                             AVCodecContext *avctx)
{
   char *p;
   int n = 1, i, size, index = 0;
   int64_t t, *pts;

   for (p = kf; *p; p++)
      if (*p == ',')
         n++;
   size = n;
   pts = av_malloc_array(size, sizeof(*pts));
   if (!pts)
   {
      av_log(NULL, AV_LOG_FATAL, "Could not allocate forced key frames array.\n");
      exit_program(1);
   }

   p = kf;
   for (i = 0; i < n; i++)
   {
      char *next = strchr(p, ',');

      if (next)
         *next++ = 0;

      if (!memcmp(p, "chapters", 8))
      {

         AVFormatContext *avf = output_files[ost->file_index]->ctx;
         int j;

         if (avf->nb_chapters > INT_MAX - size ||
             !(pts = av_realloc_f(pts, size += avf->nb_chapters - 1,
                                  sizeof(*pts))))
         {
            av_log(NULL, AV_LOG_FATAL,
                   "Could not allocate forced key frames array.\n");
            exit_program(1);
         }
         t = p[8] ? parse_time_or_die("force_key_frames", p + 8, 1) : 0;
         t = av_rescale_q(t, AV_TIME_BASE_Q, avctx->time_base);

         for (j = 0; j < avf->nb_chapters; j++)
         {
            AVChapter *c = avf->chapters[j];
            av_assert1(index < size);
            pts[index++] = av_rescale_q(c->start, c->time_base,
                                        avctx->time_base) +
                           t;
         }
      }
      else
      {

         t = parse_time_or_die("force_key_frames", p, 1);
         av_assert1(index < size);
         pts[index++] = av_rescale_q(t, AV_TIME_BASE_Q, avctx->time_base);
      }

      p = next;
   }

   av_assert0(index == size);
   qsort(pts, size, sizeof(*pts), compare_int64);
   ost->forced_kf_count = size;
   ost->forced_kf_pts = pts;
}

///////////////////////////////////

void init_output_stream(std::string &error)
{
   int ret = 0;

   if (ost->encoding_needed)
   {
      AVCodec *codec = ost->enc;
      AVCodecContext *dec = NULL;
      InputStream *ist;

      if ((ist = get_input_stream(ost)))
         dec = ist->dec_ctx;
      if (dec && dec->subtitle_header)
      {
         /* ASS code assumes this buffer is null terminated so add extra byte. */
         ost->enc_ctx->subtitle_header = av_mallocz(dec->subtitle_header_size + 1);
         if (!ost->enc_ctx->subtitle_header)
            return AVERROR(ENOMEM);
         memcpy(ost->enc_ctx->subtitle_header, dec->subtitle_header, dec->subtitle_header_size);
         ost->enc_ctx->subtitle_header_size = dec->subtitle_header_size;
      }
      if (!av_dict_get(ost->encoder_opts, "threads", NULL, 0))
         av_dict_set(&ost->encoder_opts, "threads", "auto", 0);
      if (ost->enc->type == AVMEDIA_TYPE_AUDIO &&
          !codec->defaults &&
          !av_dict_get(ost->encoder_opts, "b", NULL, 0) &&
          !av_dict_get(ost->encoder_opts, "ab", NULL, 0))
         av_dict_set(&ost->encoder_opts, "b", "128000", 0);

      if (ost->filter && ost->filter->filter->inputs[0]->hw_frames_ctx)
      {
         ost->enc_ctx->hw_frames_ctx = av_buffer_ref(ost->filter->filter->inputs[0]->hw_frames_ctx);
         if (!ost->enc_ctx->hw_frames_ctx)
            return AVERROR(ENOMEM);
      }

      if ((ret = avcodec_open2(ost->enc_ctx, codec, &ost->encoder_opts)) < 0)
      {
         if (ret == AVERROR_EXPERIMENTAL)
            abort_codec_experimental(codec, 1);
         snprintf(error, error_len,
                  "Error while opening encoder for output stream #%d:%d - "
                  "maybe incorrect parameters such as bit_rate, rate, width or height",
                  ost->file_index, ost->index);
         return ret;
      }
      if (ost->enc->type == AVMEDIA_TYPE_AUDIO &&
          !(ost->enc->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
         av_buffersink_set_frame_size(ost->filter->filter,
                                      ost->enc_ctx->frame_size);
      assert_avoptions(ost->encoder_opts);
      if (ost->enc_ctx->bit_rate && ost->enc_ctx->bit_rate < 1000)
         av_log(NULL, AV_LOG_WARNING, "The bitrate parameter is set too low."
                                      " It takes bits/s as argument, not kbits/s\n");

      ret = avcodec_parameters_from_context(ost->st->codecpar, ost->enc_ctx);
      if (ret < 0)
      {
         av_log(NULL, AV_LOG_FATAL,
                "Error initializing the output stream codec context.\n");
         exit_program(1);
      }
      /*
         * FIXME: ost->st->codec should't be needed here anymore.
         */
      ret = avcodec_copy_context(ost->st->codec, ost->enc_ctx);
      if (ret < 0)
         return ret;

      if (ost->enc_ctx->nb_coded_side_data)
      {
         int i;

         ost->st->side_data = av_realloc_array(NULL, ost->enc_ctx->nb_coded_side_data,
                                               sizeof(*ost->st->side_data));
         if (!ost->st->side_data)
            return AVERROR(ENOMEM);

         for (i = 0; i < ost->enc_ctx->nb_coded_side_data; i++)
         {
            const AVPacketSideData *sd_src = &ost->enc_ctx->coded_side_data[i];
            AVPacketSideData *sd_dst = &ost->st->side_data[i];

            sd_dst->data = av_malloc(sd_src->size);
            if (!sd_dst->data)
               return AVERROR(ENOMEM);
            memcpy(sd_dst->data, sd_src->data, sd_src->size);
            sd_dst->size = sd_src->size;
            sd_dst->type = sd_src->type;
            ost->st->nb_side_data++;
         }
      }

      // copy timebase while removing common factors
      ost->st->time_base = av_add_q(ost->enc_ctx->time_base, (AVRational){0, 1});
      ost->st->codec->codec = ost->enc_ctx->codec;
   }
   else if (ost->stream_copy)
   {
      ret = init_output_stream_streamcopy(ost);
      if (ret < 0)
         return ret;

      /*
         * FIXME: will the codec context used by the parser during streamcopy
         * This should go away with the new parser API.
         */
      ret = avcodec_parameters_to_context(ost->parser_avctx, ost->st->codecpar);
      if (ret < 0)
         return ret;
   }

   /* initialize bitstream filters for the output stream
     * needs to be done here, because the codec id for streamcopy is not
     * known until now */
   ret = init_output_bsfs(ost);
   if (ret < 0)
      return ret;

   ost->initialized = 1;

   ret = check_init_output_file(output_files[ost->file_index], ost->file_index);
   if (ret < 0)
      return ret;

   return ret;
}

////////////////////////

void OutputStream::close_output_stream()
{
   OutputFile *of = output_files[ost->file_index];

   ost->finished |= ENCODER_FINISHED;
   if (of->shortest)
   {
      int64_t end = av_rescale_q(ost->sync_opts - ost->first_pts, ost->enc_ctx->time_base, AV_TIME_BASE_Q);
      of->recording_time = FFMIN(of->recording_time, end);
   }
}

/////////////////////////

void OutputStream::finish()
{
   finished = ENCODER_FINISHED | MUXER_FINISHED;
   ost->file.finish_if_shortest();
}

void OutputStream::clear_stream()
{
   if (logfile.is_open())
      logfile.close();
   av_freep(&forced_kf_pts);
   av_freep(&apad);
   av_freep(&disposition);
   av_dict_free(&encoder_opts);
   av_dict_free(&sws_dict);
   av_dict_free(&swr_opts);
   av_dict_free(&resample_opts);
}