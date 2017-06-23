#include "ffmpegInputFile.h"

#include <cstring>

extern "C" {
#include <libavutil/time.h>
}

#include "ffmpegUtil.h"
#include "ffmpegAvRedefine.h"

using namespace ffmpeg;

InputFile::InputFile(const std::string &filename, InputOptionsContext &o, const int i)
    : ctx(NULL, delete_input_ctx), index(i), start_time(0), recording_time(0),
      input_ts_offset(0), ts_offset(0), rate_emu(false), accurate_seek(0), loop(0), duration(0), time_base({1, 1}),
      thread_queue_size(8), non_blocking(false)
{
   const std::string *opt_str;
   const bool *opt_bool;
   const int *opt_int;
   const int64_t *opt_int64;

   /* get default parameters from command line */
   ctx.reset(avformat_alloc_context());
   if (!ctx.get())
      throw ffmpegException(filename, AVERROR(ENOMEM));

   // set format's codecs to the last forced
   opt_str = o.gettype<SpecifierOptsString, std::string>("codec", "v");
   if (opt_str->size())
   {
      AVCodec *c = find_decoder(*opt_str, AVMEDIA_TYPE_VIDEO); // always return non-NULL
      ctx->video_codec_id = c->id;
      av_format_set_video_codec(ctx.get(), c);
   }
   else
   {
      ctx->video_codec_id = AV_CODEC_ID_NONE;
   }

   opt_str = o.gettype<SpecifierOptsString, std::string>("codec", "a");
   if (opt_str)
   {
      AVCodec *c = find_decoder(*opt_str, AVMEDIA_TYPE_AUDIO); // always return non-NULL
      ctx->audio_codec_id = c->id;
      av_format_set_audio_codec(ctx.get(), c);
   }
   else
   {
      ctx->audio_codec_id = AV_CODEC_ID_NONE;
   }

   opt_str = o.gettype<SpecifierOptsString, std::string>("codec", "s");
   if (opt_str)
   {
      AVCodec *c = find_decoder(*opt_str, AVMEDIA_TYPE_SUBTITLE); // always return non-NULL
      ctx->subtitle_codec_id = c->id;
      av_format_set_subtitle_codec(ctx.get(), c);
   }
   else
   {
      ctx->subtitle_codec_id = AV_CODEC_ID_NONE;
   }

   opt_str = o.gettype<SpecifierOptsString, std::string>("codec", "d");
   if (opt_str)
   {
      AVCodec *c = find_decoder(*opt_str, AVMEDIA_TYPE_DATA); // always return non-NULL
      ctx->data_codec_id = c->id;
      av_format_set_data_codec(ctx.get(), c);
   }
   else
   {
      ctx->data_codec_id = AV_CODEC_ID_NONE;
   }

   ctx->flags |= AVFMT_FLAG_NONBLOCK;
   ctx->interrupt_callback = int_cb; // from ffmpegBase

   if (opt_int64 = o.get<OptionTime, int64_t>("ss")) // start_time
      start_time = *opt_int64;
   if (opt_int64 = o.get<OptionTime, int64_t>("t")) // recording_time
      recording_time = *opt_int64;
   if (opt_int64 = o.get<OptionTime, int64_t>("itsoffset")) // input_ts_offset
      input_ts_offset = *opt_int64;
   if (opt_bool = o.get<OptionBool, bool>("re")) // rate_emu
      rate_emu = *opt_bool;
   if (opt_bool = o.get<OptionBool, bool>("accurate_seek"))
      accurate_seek = *opt_bool;
   if (opt_int = o.get<OptionInt, int>("stream_loop")) // loop
      loop = *opt_int;
   if (opt_int = o.get<OptionInt, int>("thread_queue_size"))
      thread_queue_size = *opt_int;

   ////////////////////

   bool scan_all_pmts_set = false;
   if (!av_dict_get(o.format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE))
   {
      av_dict_set(&o.format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
      scan_all_pmts_set = true;
   }

   /* open the input file with generic avformat function */
   AVFormatContext *ic = ctx.get();
   int err;
   if ((err = avformat_open_input(&ic, filename.c_str(), o.file_iformat, &o.format_opts)) < 0)
      throw ffmpegException(filename, err);
   ctx.reset(ic);

   if (scan_all_pmts_set)
      av_dict_set(&o.format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

   ////////////////////

   // remove format options that are mutula in codec options
   remove_avoptions(o.format_opts, o.codec_opts);
   assert_avoptions(o.format_opts); // if format options are not completely consumed, throws an exception

   ////////////////////

   /* apply forced codec ids */
   for (int i = 0; i < (int)ctx->nb_streams; i++)
      o.choose_decoder(ctx.get(), ctx->streams[i]);

   // Read packets of a media file to populate the stream information
   o.find_stream_info(ctx.get());

   // override start time (ss) if sseof (start time w.r.t. end of file) is given
   if ((opt_int64 = o.get<OptionTime, int64_t>("sseof")) && *opt_int64 != AV_NOPTS_VALUE) // start_time_eof
   {
      if (ctx->duration > 0)
         o.set<OptionTime, int64_t>("ss", *opt_int64 + ctx->duration);
      // else
      //    av_log(NULL, AV_LOG_WARNING, "Cannot use -sseof, duration of %s not known\n", filename);
   }

   bool do_seek = false;
   if (opt_int64 = o.get<OptionTime, int64_t>("ss")) // start_time
   {
      do_seek = opt_int64 && *opt_int64 != AV_NOPTS_VALUE;
   }

   int64_t timestamp;
   if (do_seek)
   {
      timestamp = do_seek ? *opt_int64 : 0;

      /* add the stream start time */
      if ((opt_bool = o.get<OptionBool, bool>("seek_timestamp")) && *opt_bool && ctx->start_time != AV_NOPTS_VALUE)
         timestamp += ctx->start_time;

      /* execute the seek */
      seek(timestamp);
   }

   // set timestamp offset
   ts_offset = -(copy_ts ? (start_at_zero && ctx->start_time != AV_NOPTS_VALUE ? ctx->start_time : 0) : timestamp);
   if (opt_int64 = o.get<OptionTime, int64_t>("itsoffset")) // input_ts_offset
      ts_offset += *opt_int64;

   /* update the current parameters so that they match the one of the input stream */
   add_input_streams(o);

   /* dump the file content */
   //av_dump_format(ctx, nb_input_files, filename, 0);

   /* check if all codec options have been used */
   DictPtr unused_opts = strip_specifiers(o.codec_opts); // ffmpegUtil.h
   AVDictionary *d = unused_opts.release();
   for (auto stream = streams.begin(); stream < streams.end(); stream++)
      (*stream)->remove_used_opts(d);
   unused_opts.reset(d);

   AVDictionaryEntry *e = NULL;
   while ((e = av_dict_get(d, "", e, AV_DICT_IGNORE_SUFFIX)))
   {
      const AVClass *avclass = avcodec_get_class();
      const AVOption *option = av_opt_find(&avclass, e->key, NULL, 0, AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
      const AVClass *fclass = avformat_get_class();
      const AVOption *foption = av_opt_find(&fclass, e->key, NULL, 0, AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
      if (!option || foption)
         continue;

      if (!(option->flags & AV_OPT_FLAG_DECODING_PARAM))
      {
         std::ostringstream msg;
         msg << "Codec AVOption " << e->key << " (" << (option->help ? option->help : "")
             << ") specified for input file #" << i << " (" << filename << ") is not a decoding option.";
         throw ffmpegException(msg.str());
      }

      // av_log(NULL, AV_LOG_WARNING, "Codec AVOption %s (%s) specified for "
      //                              "input file #%d (%s) has not been used for any stream. The most "
      //                              "likely reason is either wrong type (e.g. a video option with "
      //                              "no video streams) or that it is a private option of some decoder "
      //                              "which was not actually used for any stream.\n",
      //        e->key,
      //        option->help ? option->help : "", nb_input_files - 1, filename);
   }

   // if (const SpecifierOptsString *specopt = (const SpecifierOptsString *)o.find("dump_attachment"))
   // {
   //    for (auto s = specopt->begin(); s!=specopt->end(); s++)
   //    {
   //       for (int j = 0; j < ctx->nb_streams; j++)
   //       {
   //          AVStream *st = ctx->streams[j];
   //          try
   //          {
   //             dump_attachment(st, specopt->get(ctx.get(),st));
   //          }
   //          catch(...){}
   //       }
   //    }
   // }

   input_stream_potentially_available = 1;
}

void InputFile::seek(const int64_t timestamp)
{
   int64_t seek_timestamp = timestamp;
   if (!(ctx->iformat->flags & AVFMT_SEEK_TO_PTS))
   {
      int dts_heuristic = 0;
      for (int i = 0; i < (int)ctx->nb_streams; i++)
      {
         if (ctx->streams[i]->codecpar->video_delay)
            dts_heuristic = 1;
      }
      if (dts_heuristic)
         seek_timestamp -= 3 * AV_TIME_BASE / 23;
   }

   if (avformat_seek_file(ctx.get(), -1, INT64_MIN, seek_timestamp, seek_timestamp, 0) < 0)
   {
      std::ostringstream msg;
      msg << "Could not seek to position " << (double)timestamp / AV_TIME_BASE;
      throw ffmpegException(msg.str());
   }
}

/* Add all the streams from the given input file to the global
 * list of input streams. */
void InputFile::add_input_streams(InputOptionsContext &o)
{
   AVFormatContext *ic = ctx.get();

   for (int i = 0; i < (int)ic->nb_streams; i++) // for each stream
   {
      switch (ic->streams[i]->codecpar->codec_type)
      {
      case AVMEDIA_TYPE_VIDEO:
         streams.emplace_back(new VideoInputStream(*this, i, o));
         break;
      case AVMEDIA_TYPE_AUDIO:
         streams.emplace_back(new AudioInputStream(*this, i, o));
         break;
      case AVMEDIA_TYPE_SUBTITLE:
         streams.emplace_back(new SubtitleInputStream(*this, i, o));
         break;
      case AVMEDIA_TYPE_DATA:
         streams.emplace_back(new DataInputStream(*this, i, o));
         break;
      case AVMEDIA_TYPE_ATTACHMENT:
      case AVMEDIA_TYPE_UNKNOWN:
         streams.emplace_back(new InputStream(*this, i, o));
         break;
      default:
         throw ffmpegException("Unsupported decoder media type.");
      }
   }
}

////////////////////////////

int InputFile::get_packet_once(AVPacket &pkt)
{
   if (rate_emu)
   {
      for (auto ist = streams.begin(); ist < streams.end(); ist++)
         (*ist)->assert_emu_dts();
   }

   return av_thread_message_queue_recv(in_thread_queue, &pkt, non_blocking ? AV_THREAD_MESSAGE_NONBLOCK : 0);
   // return av_read_frame(f->ctx, pkt); // << use this if not using thread
}

int InputFile::get_packet(AVPacket &pkt)
{
   // receive the frame/packet from one of input threads
   int ret = get_packet_once(pkt);
   if (ret == AVERROR(EAGAIN)) // frame not available now
   {
      eagain = true;
      return ret;
   }
   if (ret==AVERROR_EOF && loop) // if reached EOF and loop playback mode
   {
      if ((ret = seek_to_start()) < 0) // rewind
         return ret;
      ret = get_packet_once(pkt); // see if the first frame is already available
      if (ret == AVERROR(EAGAIN))
      {
         eagain = true;
         return ret;
      }
   }
   if (ret==AVERROR_EOF) // if end-of-file
   {
      // if end-of-file reached, flush all input & output streams
      for (auto ist = streams.begin(); ist < streams.end(); ist++)
         (*ist)->flush(false);
      
      eof_reached = true;
      return AVERROR(EAGAIN);
   }
   else if (ret < 0) // if not end-of-file, a fatal error has occurred
   {
      throw ffmpegException(ctx->filename, ret);
   }
   return ret;
}

/*
 * Return
 * - 0 -- one packet was read and processed
 * - AVERROR(EAGAIN) -- no packets were available for selected file,
 *   this function should be called again
 * - AVERROR_EOF -- this function should not be called again
 */
void InputFile::prepare_packet(AVPacket &pkt, InputStream *&ist)
{
   ist = NULL;
   if (pkt.stream_index < streams.size())
   {
      ist = streams[pkt.stream_index].get();
      if (ist->process_packet_time(pkt, ts_offset, last_ts))
         ist->prepare_packet(&pkt, false); // decode if requested
   }
}

/////////////////////////////////////////////////////////

void InputFile::input_thread()
{
   unsigned flags = non_blocking ? AV_THREAD_MESSAGE_NONBLOCK : 0;
   int ret = 0;

   while (1)
   {
      AVPacket pkt;

      // read next frame
      ret = av_read_frame(ctx.get(), &pkt);
      if (ret == AVERROR(EAGAIN)) // frame not ready, wait 10 ms
      {
         av_usleep(10000);
         continue;
      }
      if (ret < 0) // unexcepted error occurred, throw the error over the message queue and quit
      {
         av_thread_message_queue_set_err_recv(in_thread_queue, ret);
         break;
      }

      // send decoded frame to the message queue
      ret = av_thread_message_queue_send(in_thread_queue, &pkt, flags);
      if (flags && ret == AVERROR(EAGAIN)) // queue overflowed
      {
         flags = 0;
         ret = av_thread_message_queue_send(in_thread_queue, &pkt, flags);
         //av_log(ctx, AV_LOG_WARNING, "Thread message queue blocking; consider raising the thread_queue_size option (current value: %d)\n", thread_queue_size);
      }
      if (ret < 0)
      {
         // if (ret != AVERROR_EOF)
         //    av_log(ctx, AV_LOG_ERROR, "Unable to send packet to main thread: %s\n", av_err2str(ret));
         av_packet_unref(&pkt); // let go of the packet object
         av_thread_message_queue_set_err_recv(in_thread_queue, ret);
         break;
      }
   }
}

void InputFile::init_thread()
{
   if (ctx->pb ? !ctx->pb->seekable : std::strcmp(ctx->iformat->name, "lavfi"))
      non_blocking = true;

   int ret;
   if (ret = av_thread_message_queue_alloc(&in_thread_queue, thread_queue_size, sizeof(AVPacket)) < 0)
      throw ffmpegException(ret);

   thread = std::thread(&InputFile::input_thread, this);
}

void InputFile::free_thread(void)
{
   AVPacket pkt;

   if (!in_thread_queue)
      return;

   av_thread_message_queue_set_err_send(in_thread_queue, AVERROR_EOF);
   while (av_thread_message_queue_recv(in_thread_queue, &pkt, 0) >= 0)
      av_packet_unref(&pkt);

   thread.join();
   joined = true;
   av_thread_message_queue_free(&in_thread_queue);
}

//////////////////////

int InputFile::seek_to_start()
{
   int ret;
   bool has_audio = false;
   
   // do the seeking
   ret = av_seek_frame(ctx.get(), -1, ctx->start_time, 0);
   if (ret < 0)
      return ret;

   for (auto pist = streams.begin(); pist < streams.end(); pist++)
   {
      InputStream *ist = pist->get();

      // flush decoders
      ist->flush();

      /* duration is the length of the last frame in a stream
         * when audio stream is present we don't care about
         * last video frame length because it's not defined exactly */
      has_audio = ist->has_audio_samples();
   }

   for (auto pist = streams.begin(); pist < streams.end(); pist++)
   {
      InputStream *ist = pist->get();

      AVRational *tb; // timebase
      int64_t T = ist->get_duration(has_audio,tb); // get duration & time base

      // if duration has not been set for the file, set its time base to that of the current stream's
      if (!duration)
         time_base = *tb;

      /* the total duration of the stream, max_pts - min_pts is
         * the duration of the stream without the last frame */
      time_base = duration_max(T, duration, *tb, time_base);
   }

   if (loop > 0)
      loop--;

   return ret;
}

void InputFile::update_start_time()
{
   // Correcting starttime based on the enabled streams
   // FIXME this ideally should be done before the first use of starttime but we do not know which are the enabled streams at that point.
   //       so we instead do it here as part of discontinuity handling
   if (ts_offset == -ctx->start_time && (ctx->iformat->flags & AVFMT_TS_DISCONT))
   {
      int64_t new_start_time = INT64_MAX;
      for (unsigned int i = 0; i < ctx->nb_streams; i++)
      {
         AVStream *st = ctx->streams[i];
         if (st->discard == AVDISCARD_ALL || st->start_time == AV_NOPTS_VALUE)
            continue;
         new_start_time = FFMIN(new_start_time, av_rescale_q(st->start_time, st->time_base, AV_TIME_BASE_Q));
      }
      if (new_start_time > ctx->start_time)
      {
         //av_log(ctx, AV_LOG_VERBOSE, "Correcting start time by %" PRId64 "\n", new_start_time - is->start_time);
         ts_offset = -new_start_time;
      }
   }
}
