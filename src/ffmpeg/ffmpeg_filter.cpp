/*
 * ffmpeg filter configuration
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

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"

#include "libavresample/avresample.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/display.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"

// void choose_sample_fmt(AVStream *st, AVCodec *codec)
// {
//     if (codec && codec->sample_fmts) {
//         const enum AVSampleFormat *p = codec->sample_fmts;
//         for (; *p != -1; p++) {
//             if (*p == st->codecpar->format)
//                 break;
//         }
//         if (*p == -1) {
//             if((codec->capabilities & AV_CODEC_CAP_LOSSLESS) && av_get_sample_fmt_name(st->codecpar->format) > av_get_sample_fmt_name(codec->sample_fmts[0]))
//                 av_log(NULL, AV_LOG_ERROR, "Conversion will not be lossless.\n");
//             if(av_get_sample_fmt_name(st->codecpar->format))
//             av_log(NULL, AV_LOG_WARNING,
//                    "Incompatible sample format '%s' for codec '%s', auto-selecting format '%s'\n",
//                    av_get_sample_fmt_name(st->codecpar->format),
//                    codec->name,
//                    av_get_sample_fmt_name(codec->sample_fmts[0]));
//             st->codecpar->format = codec->sample_fmts[0];
//         }
//     }
// }

int init_simple_filtergraph(InputStream &ist, OutputStream &ost)
{
    filtergraphs.emplace_back(ist,ost);
    return 0;
}

