#pragma once

extern "C"
{
#include <libavformat/avformat.h>
}

struct InputFile
{
    // accessed from transcode.cpp
    AVFormatContext *ctx;
    int eof_reached; /* true if eof reached */
    int eagain;      /* true if last read attempt returned EAGAIN */
    int ist_index;   /* index of first stream in input_streams */
    int nb_streams;  /* number of stream that ffmpeg is aware of; may be different */
    int rate_emu;

    int loop;                /* set number of times input stream should be looped */
    int64_t duration;        /* actual duration of the longest stream in a file
                             at the moment when looping happens */
    AVRational time_base;    /* time base of the duration */
    int64_t input_ts_offset; // OF

    int64_t ts_offset;
    int64_t last_ts;
    int64_t start_time; /* FI user-specified start time in AV_TIME_BASE or AV_NOPTS_VALUE */
    // int seek_timestamp;
    int64_t recording_time; // FI
    //                          from ctx.nb_streams if new streams appear during av_read_frame() */
    int nb_streams_warn; /* number of streams that the user was warned of */
    int accurate_seek;   // FI
};

int process_input(int file_index);
