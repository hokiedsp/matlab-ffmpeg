/*
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

#ifndef FFMPEG_H
#define FFMPEG_H

#include "config.h"

#include <cstdint>
#include <csignal>

#include <vector>
#include <thread>

#include "cmdutils.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavformat/avio.h"

#include "libavcodec/avcodec.h"

#include "libavfilter/avfilter.h"

#include "libavutil/avutil.h"
#include "libavutil/dict.h"
#include "libavutil/eval.h"
#include "libavutil/fifo.h"
#include "libavutil/pixfmt.h"
#include "libavutil/rational.h"
#include "libavutil/threadmessage.h"

#include "libswresample/swresample.h"
}

typedef struct InputStream {

    int           wrap_correction_done;

    int64_t filter_in_rescale_delta_last;

    struct { /* previous decoded subtitle and related variables */
        int got_output;
        int ret;
        AVSubtitle subtitle;
    } prev_sub;

    struct sub2video {
        int64_t last_pts;
        int64_t end_pts;
        AVFrame *frame;
        int w, h;
    } sub2video;

    int dr1;

    /* stats */
    // number of frames/samples retrieved from the decoder
    uint64_t frames_decoded;
    uint64_t samples_decoded;

    int64_t *dts_buffer;
    int nb_dts_buffer;
} InputStream;

typedef struct InputFile {
    int nb_streams_warn;  /* number of streams that the user was warned of */

} InputFile;

#define VSYNC_AUTO -1
#define VSYNC_PASSTHROUGH 0
#define VSYNC_CFR 1
#define VSYNC_VFR 2
#define VSYNC_VSCFR 0xfe
#define VSYNC_DROP 0xff

#define MAX_STREAMS 1024 /* arbitrary sanity check value */

enum forced_keyframes_const
{
   FKF_N,
   FKF_N_FORCED,
   FKF_PREV_FORCED_N,
   FKF_PREV_FORCED_T,
   FKF_T,
   FKF_NB
};

#define ABORT_ON_FLAG_EMPTY_OUTPUT (1 << 0)

void term_init(void);
void term_exit(void);

void reset_options(OptionsContext *o, int is_input);
void show_usage(void);

void opt_output_file(void *optctx, const char *filename);

void remove_avoptions(AVDictionary **a, AVDictionary *b);
void assert_avoptions(AVDictionary *m);

int guess_input_channel_layout(InputStream *ist);

enum AVPixelFormat choose_pixel_fmt(AVStream *st, AVCodecContext *avctx, AVCodec *codec, enum AVPixelFormat target);
void choose_sample_fmt(AVStream *st, AVCodec *codec);

int ffmpeg_parse_options(int argc, char **argv);

int vdpau_init(AVCodecContext *s);
int dxva2_init(AVCodecContext *s);
int vda_init(AVCodecContext *s);
int videotoolbox_init(AVCodecContext *s);
int qsv_init(AVCodecContext *s);
int qsv_transcode_init(OutputStream *ost);
int vaapi_decode_init(AVCodecContext *avctx);
int vaapi_device_init(const char *device);
int cuvid_init(AVCodecContext *s);
int cuvid_transcode_init(OutputStream *ost);

#endif /* FFMPEG_H */
