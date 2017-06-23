#include "ffmpegInputStream.h"

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavfilter/buffersrc.h>
}

#include "ffmpegInputFile.h"
#include "ffmpegAvRedefine.h"

using namespace ffmpeg;

// HW Acceleration
const HWAccels VideoInputStream::hwaccels = {
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
};

const HWAccel *VideoInputStream::get_hwaccel(enum AVPixelFormat pix_fmt)
{
    for (auto it = hwaccels.begin(); it<hwaccels.end(); it++)
        if (it->pix_fmt == pix_fmt)
            return &*it;
    return NULL;
}

VideoInputStream::VideoInputStream(InputFile &f, const int i, const InputOptionsContext &o)
    : InputStream(f, i, o), framerate({0, 0}), resample_height(dec_ctx->height), resample_width(dec_ctx->width),
      resample_pix_fmt(dec_ctx->pix_fmt), autorotate(true), top_field_first(-1), hwaccel_id(HWACCEL_NONE),
      hwaccel_output_format(AV_PIX_FMT_NONE), hwaccel_pix_fmt(AV_PIX_FMT_NONE)
{
    const std::string *opt_str;
    const int *opt_int;
    const Option *opt = NULL;

    if (opt = o.cfind("autorotate"))
        autorotate = ((OptionBool *)opt)->value;

    // if decoder has not been forced, set it automatically
    if (!dec)
        dec = avcodec_find_decoder(st->codecpar->codec_id);

    // sync stream & decoder context data
    dec_ctx->framerate = st->avg_frame_rate;

    // set custom frame rate
    if ((opt_str = o.getspec<SpecifierOptsString, std::string>("r", file.ctx.get(), st)) &&
        (av_parse_video_rate(&framerate, opt_str->c_str()) < 0))
        throw ffmpegException("Error parsing framerate: " + *opt_str + ".");

    if (opt_int = o.getspec<SpecifierOptsInt, int>("top", file.ctx.get(), st))
        top_field_first = *opt_int;

    if (opt_str = o.getspec<SpecifierOptsString, std::string>("hwaccel", file.ctx.get(), st))
    {
        if (*opt_str == "none")
            hwaccel_id = HWACCEL_NONE;
        else if (*opt_str == "auto")
            hwaccel_id = HWACCEL_AUTO;
        else
        {
            for (auto hwaccel = hwaccels.begin(); hwaccel < hwaccels.end(); hwaccel++)
            {
                if (*opt_str == hwaccel->name)
                {
                    hwaccel_id = hwaccel->id;
                    break;
                }
            }
            if (hwaccel_id == HWACCEL_NONE)
                throw ffmpegException("Unrecognized hwaccel: " + *opt_str + ".");
        }
    }

    if (opt_str = o.getspec<SpecifierOptsString, std::string>("hwaccel_device", file.ctx.get(), st))
    {
        hwaccel_device = *opt_str;
        if (hwaccel_device.empty())
            throw ffmpegException("hwaccel_device not given");
    }

    if (opt_str = o.getspec<SpecifierOptsString, std::string>("hwaccel_output_format", file.ctx.get(), st))
    {
        hwaccel_output_format = av_get_pix_fmt(opt_str->c_str());
        if (hwaccel_output_format == AV_PIX_FMT_NONE)
            throw ffmpegException("Unrecognised hwaccel output format: " + *opt_str + ".");
    }

    // update the parameter
    if (avcodec_parameters_from_context(st->codecpar, dec_ctx.get()) < 0)
        throw ffmpegException("Error initializing the decoder context.");
}

///////////////////////////////////////////////////////////////////////////////////////////////////

int VideoInputStream::decode_packet(const AVPacket *inpkt, bool repeating, bool &got_output)
{
    int ret;

    AVPacket avpkt;
    if (!inpkt)
    {
        /* EOF handling */
        av_init_packet(&avpkt);
        avpkt.data = NULL;
        avpkt.size = 0;
    }
    else
    {
        avpkt = *inpkt;
    }

    AVPacket *pkt = repeating ? NULL : &avpkt;

    //ret = decode_video(ist, repeating ? NULL : &avpkt, &got_output, !pkt);
    //int decode_video(InputStream * ist, AVPacket * pkt, int *got_output, int eof)
    {
        AVFrame *decoded_frame, *f;
        int i, ret = 0, err = 0, resample_changed;
        int64_t best_effort_timestamp;
        int64_t dts = AV_NOPTS_VALUE;
        AVRational *frame_sample_aspect;
        AVPacket avpkt;

        // With fate-indeo3-2, we're getting 0-sized packets before EOF for some
        // reason. This seems like a semi-critical bug. Don't trigger EOF, and
        // skip the packet.
        if (!eof && pkt && pkt->size == 0)
            return 0;

        if (!decoded_frame && !(decoded_frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
        if (!filter_frame && !(filter_frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
        decoded_frame = decoded_frame;
        if (dts != AV_NOPTS_VALUE)
            dts = av_rescale_q(dts, AV_TIME_BASE_Q, st->time_base);
        if (pkt)
        {
            avpkt = *pkt;
            avpkt.dts = dts; // ffmpeg.c probably shouldn't do this
        }

        // The old code used to set dts on the drain packet, which does not work
        // with the new API anymore.
        if (eof)
        {
            dts_buffer = (int64_t *)av_realloc_array(dts_buffer, nb_dts_buffer + 1, sizeof(dts_buffer[0]));
            if (!dts_buffer)
                return AVERROR(ENOMEM);
            dts_buffer[nb_dts_buffer++] = dts;
        }

        // update_benchmark(NULL);
        ret = decode(dec_ctx.get(), decoded_frame, got_output, pkt ? &avpkt : NULL);
        // update_benchmark("decode_video %d.%d", file_index, st->index);

        // The following line may be required in some cases where there is no parser
        // or the parser does not has_b_frames correctly
        if (st->codecpar->video_delay < dec_ctx->has_b_frames)
        {
            if (dec_ctx->codec_id == AV_CODEC_ID_H264)
            {
                st->codecpar->video_delay = dec_ctx->has_b_frames;
            }
            else
                av_log(dec_ctx.get(), AV_LOG_WARNING,
                       "video_delay is larger in decoder than demuxer %d > %d.\n"
                       "If you want to help, upload a sample "
                       "of this file to ftp://upload.ffmpeg.org/incoming/ "
                       "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)",
                       dec_ctx->has_b_frames,
                       st->codecpar->video_delay);
        }

        if (ret != AVERROR_EOF)
            check_decode_result(got_output, ret);

        // if (*got_output && ret >= 0)
        // {
        //    if (dec_ctx->width != decoded_frame->width || dec_ctx->height != decoded_frame->height || dec_ctx->pix_fmt != decoded_frame->format)
        //    {
        //       av_log(NULL, AV_LOG_DEBUG, "Frame parameters mismatch context %d,%d,%d != %d,%d,%d\n",
        //              decoded_frame->width, decoded_frame->height, decoded_frame->format, dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt);
        //    }
        // }

        if (!got_output || ret < 0)
            return ret;

        if (top_field_first >= 0)
            decoded_frame->top_field_first = top_field_first;

        frames_decoded++;

        if (hwaccel_retrieve_data && decoded_frame->format == hwaccel_pix_fmt)
        {
            err = hwaccel_retrieve_data(dec_ctx.get(), decoded_frame);
            if (err < 0)
            {
                av_frame_unref(filter_frame);
                av_frame_unref(decoded_frame);
                return err < 0 ? err : ret;
            }
        }

        hwaccel_retrieved_pix_fmt = (AVPixelFormat)decoded_frame->format;

        best_effort_timestamp = av_frame_get_best_effort_timestamp(decoded_frame);

        if (eof && best_effort_timestamp == AV_NOPTS_VALUE && nb_dts_buffer > 0)
        {
            best_effort_timestamp = dts_buffer[0];

            for (i = 0; i < nb_dts_buffer - 1; i++)
                dts_buffer[i] = dts_buffer[i + 1];
            nb_dts_buffer--;
        }

        if (best_effort_timestamp != AV_NOPTS_VALUE)
        {
            int64_t ts = av_rescale_q(decoded_frame->pts = best_effort_timestamp, st->time_base, AV_TIME_BASE_Q);

            if (ts != AV_NOPTS_VALUE)
                next_pts = pts = ts;
        }

        // if (debug_ts)
        // {
        //    av_log(NULL, AV_LOG_INFO, "decoder -> ist_index:%d type:video "
        //                              "frame_pts:%s frame_pts_time:%s best_effort_ts:%" PRId64 " best_effort_ts_time:%s keyframe:%d frame_type:%d time_base:%d/%d\n",
        //           st->index, av_ts2str(decoded_frame->pts),
        //           av_ts2timestr(decoded_frame->pts, &st->time_base),
        //           best_effort_timestamp,
        //           av_ts2timestr(best_effort_timestamp, &st->time_base),
        //           decoded_frame->key_frame, decoded_frame->pict_type,
        //           st->time_base.num, st->time_base.den);
        // }

        if (st->sample_aspect_ratio.num)
            decoded_frame->sample_aspect_ratio = st->sample_aspect_ratio;

        resample_changed = resample_width != decoded_frame->width || resample_height != decoded_frame->height || resample_pix_fmt != decoded_frame->format;
        if (resample_changed)
        {
            // av_log(NULL, AV_LOG_INFO,
            //        "Input stream #%d:%d frame changed from size:%dx%d fmt:%s to size:%dx%d fmt:%s\n",
            //        file_index, st->index,
            //        resample_width, resample_height, av_get_pix_fmt_name(resample_pix_fmt),
            //        decoded_frame->width, decoded_frame->height, av_get_pix_fmt_name(decoded_frame->format));

            resample_width = decoded_frame->width;
            resample_height = decoded_frame->height;
            resample_pix_fmt = decoded_frame->format;

            for (auto filt = filters.begin(); filt < filters.end(); filt++)
            {
                if (reinit_filters && (*filt)->graph.configure_filtergraph() < 0)
                    throw ffmpegException("Error reinitializing filters!");
            }
        }

        frame_sample_aspect = (AVRational *)av_opt_ptr(avcodec_get_frame_class(), decoded_frame, "sample_aspect_ratio");
        for (auto filt = filters.begin(); filt < filters.end(); filt++)
        {
            if (!frame_sample_aspect->num)
                *frame_sample_aspect = st->sample_aspect_ratio;

            if (*filt != filters.back())
            {
                f = filter_frame;
                err = av_frame_ref(f, decoded_frame);
                if (err < 0)
                    break;
            }
            else
                f = decoded_frame;
            err = av_buffersrc_add_frame_flags((*filt)->filter, f, AV_BUFFERSRC_FLAG_PUSH);
            if (err == AVERROR_EOF)
            {
                err = 0; /* ignore */
            }
            else if (err < 0)
            {
                throw ffmpegException("Failed to inject frame into filter network: " + std::string(av_err2str(err)));
            }
        }
    }

    if (!repeating || !pkt || got_output)
    {
        int64_t duration;
        if (pkt && pkt->duration)
        {
            duration = av_rescale_q(pkt->duration, st->time_base, AV_TIME_BASE_Q);
        }
        else if (dec_ctx->framerate.num != 0 && dec_ctx->framerate.den != 0)
        {
            int ticks = av_stream_get_parser(st) ? av_stream_get_parser(st)->repeat_pict + 1 : dec_ctx->ticks_per_frame;
            duration = ((int64_t)AV_TIME_BASE * dec_ctx->framerate.den * ticks) / (dec_ctx->framerate.num * dec_ctx->ticks_per_frame);
        }

        if (dts != AV_NOPTS_VALUE && duration)
            next_dts += duration;
        else
            next_dts = AV_NOPTS_VALUE;
        if (got_output)
            next_pts += duration; //FIXME the duration is not correct in some cases
    }
}

// return true to break the search in InputStream::get_format() callback
bool VideoInputStream::get_hwaccel_format(const AVPixelFormat *pix_fmt, bool &unknown)
{
    const HWAccel *hwaccel = get_hwaccel(*pix_fmt);
    unknown = false;

    if (!hwaccel ||
        (active_hwaccel_id && active_hwaccel_id != hwaccel->id) ||
        (hwaccel_id != HWACCEL_AUTO && hwaccel_id != hwaccel->id))
        return false;

    if (hwaccel->init(dec_ctx.get()) < 0)
    {
        if (hwaccel_id == hwaccel->id)
        {
            av_log(NULL, AV_LOG_FATAL,
                   "%s hwaccel requested for input stream #%d:%d, "
                   "but cannot be initialized.\n",
                   hwaccel->name,
                   file.index, st->index);
            unknown = false;
            return true; // break the loop and set format to AV_PIX_FMT_NONE
        }
        return false;
    }

    if (hw_frames_ctx)
    {
        dec_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ctx);
        if (!dec_ctx->hw_frames_ctx)
        {
            unknown = false;
            return true; // break the loop and set format to AV_PIX_FMT_NONE
        }
    }

    // set hwaccel related member variables
    active_hwaccel_id = hwaccel->id;
    hwaccel_pix_fmt = *pix_fmt;

    return true;
}

int VideoInputStream::get_stream_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    if (hwaccel_get_buffer && frame->format == hwaccel_pix_fmt)
        return hwaccel_get_buffer(s, frame, flags);

    return avcodec_default_get_buffer2(s, frame, flags);
}

void VideoInputStream::close()
{
    if (decoding_needed)
    {
        avcodec_close(dec_ctx.get());
        if (hwaccel_uninit)
            hwaccel_uninit(dec_ctx.get());
    }
}

/* pkt = NULL means EOF (needed to flush decoder buffers) */
int VideoInputStream::prepare_packet(const AVPacket *pkt, bool no_eof)
{
    int ret = InputStream::prepare_packet(pkt, no_eof);

    if (!decoding_needed) // if copying set next_dts & next_pts per stream info
    {
        if (framerate.num)
        {
            // TODO: Remove work-around for c99-to-c89 issue 7
            AVRational time_base_q = AV_TIME_BASE_Q;
            int64_t next_dts = av_rescale_q(next_dts, time_base_q, av_inv_q(framerate));
            next_dts = av_rescale_q(next_dts + 1, av_inv_q(framerate), time_base_q);
        }
        else if (pkt->duration)
        {
            next_dts += av_rescale_q(pkt->duration, st->time_base, AV_TIME_BASE_Q);
        }
        else if (dec_ctx->framerate.num != 0)
        {
            int ticks = av_stream_get_parser(st) ? av_stream_get_parser(st)->repeat_pict + 1 : dec_ctx->ticks_per_frame;
            next_dts += ((int64_t)AV_TIME_BASE * dec_ctx->framerate.den * ticks) / (dec_ctx->framerate.num * dec_ctx->ticks_per_frame);
        }
        next_pts = next_dts;
    }

    return ret;
}

AVRational VideoInputStream::get_framerate() const
{
    return framerate.num ? framerate : av_guess_frame_rate(file.ctx.get(), st, NULL);
}
