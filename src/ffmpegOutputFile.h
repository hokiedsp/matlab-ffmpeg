#pragma once

#include <vector>

extern "C" {
#include <libavformat/avformat.h> // for AVFormatContext
#include <libavutil/dict.h>       // for AVDictionary
}

#include "ffmpegBase.h"
#include "ffmpegOutputStream.h"

namespace ffmpeg
{

struct OutputStream;
struct InputStream;

struct OutputFile : public ffmpegBase
{
   int index; /* file index */
   OutputStreams streams;

   AVFormatContext *ctx;
   AVDictionary *opts;
   int64_t recording_time;  ///< desired length of the resulting file in microseconds == AV_TIME_BASE units
   int64_t start_time;      ///< start time in microseconds == AV_TIME_BASE units
   uint64_t limit_filesize; /* filesize limit expressed in bytes */

   int shortest;

   int header_written;

   OutputFile(const std::string filename, const int i, OptionsContext &o);
   ~OutputFile();

   OutputStream &new_output_stream(OptionsContext &o, AVMediaType type, InputStream *src = NULL);
   OutputStream &new_video_stream(OptionsContext &o, InputStream *src);
   OutputStream &new_audio_stream(OptionsContext &o, InputStream *src);
   OutputStream &new_data_stream(OptionsContext &o, InputStream *src);
   OutputStream &new_attachment_stream(OptionsContext &o, InputStream *src);
   OutputStream &new_subtitle_stream(OptionsContext &o, InputStream *src);
   OutputStream &new_unknown_stream(OptionsContext &o, InputStream *src);

   void set_encoder_id(OutputStream &ost);
   //void set_encoder_id(OutputFile *of, OutputStream *ost)

   //int check_init_output_file(OutputFile *of, int file_index)
   int check_init_output_file(int file_index);

   //void close_output_stream(OutputStream *ost)
   void close_output_stream();

   //void output_packet(OutputFile *of, AVPacket *pkt, OutputStream *ost);
   void output_packet(AVPacket *pkt, OutputStream *ost);

   void do_subtitle_out(OutputFile *of, OutputStream *ost, AVSubtitle *sub);
   void do_audio_out(OutputFile *of, OutputStream *ost, AVFrame *frame);

   void finish_if_shortest(); // split from finish_output_stream()
 private:
};

typedef std::vector<OutputFile> OutputFiles;
typedef std::vector<OutputFile &> OutputFileRefs;
}
