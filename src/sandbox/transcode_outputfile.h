#pragma once

extern "C"
{
#include <libavformat/avformat.h>
}

#include "transcode_outputstream.h"

struct OutputFile
{
    AVFormatContext *ctx; // T
    AVDictionary *opts;
    int ost_index; /* T index of the first stream in output_streams */
    int64_t recording_time;  ///OS < desired length of the resulting file in microseconds == AV_TIME_BASE units
    int64_t start_time;      ///< Tstart time in microseconds == AV_TIME_BASE units
    uint64_t limit_filesize; /* T filesize limit expressed in bytes */

    int shortest; // OS

    int header_written;
};

int check_init_output_file(OutputFile *of, int file_index);
OutputStream *choose_output(void); // likely need to split between top level and output file level
void output_packet(OutputFile *of, AVPacket *pkt, OutputStream *ost, int eof);
void do_video_out(OutputFile *of, OutputStream *ost, AVFrame *next_picture, double sync_ipts);
void do_audio_out(OutputFile *of, OutputStream *ost, AVFrame *frame);
void do_subtitle_out(OutputFile *of, OutputStream *ost, AVSubtitle *sub); // from transcode_inputstream.cpp!!
void set_encoder_id(OutputFile *of, OutputStream *ost);
