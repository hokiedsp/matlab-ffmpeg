#pragma once

#include <vector>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <mex.h>

#include "FFmpegInputStream.h"

struct FFmpegInputFile
{
  AVFormatContext *fmt_ctx;

  std::vector<FFmpegInputStream> streams;

  FFmpegInputFile(const char *filename = nullptr) : fmt_ctx(nullptr)
  {
    if (filename)
      open(filename);
  }
  FFmpegInputFile(const FFmpegInputFile &) = delete;            // non construction-copyable
  FFmpegInputFile(FFmpegInputFile &&src) : fmt_ctx(src.fmt_ctx) // move xtor
  {
    src.fmt_ctx = nullptr;
    streams = std::move(src.streams);
  }

  FFmpegInputFile &operator=(const FFmpegInputFile &) = delete; // non copyable
  FFmpegInputFile &operator=(FFmpegInputFile &&src) = delete;   // non movable

  ~FFmpegInputFile() { close(); }

  void open(const char *filename, AVInputFormat *iformat = nullptr, AVDictionary *format_opts = nullptr);
  void close();

  std::vector<std::string> getMediaTypes() const;

  double getDuration() const;

  /* Find the "best" stream in the file.
   * The best stream is determined according to various heuristics as the most
   * likely to be what the user expects.
   *
   * @param type              stream type: video, audio, subtitles, etc.
   * @param wanted_stream_nb  user-requested stream number,
   *                          or -1 for automatic selection
   * 
   * @return  the non-negative stream number in case of success,
   *          AVERROR_STREAM_NOT_FOUND if no stream with the requested type
   *          could be found,
   *          AVERROR_DECODER_NOT_FOUND if streams were found but no decoder
   */
  int getStreamIndex(const enum AVMediaType type, int wanted_stream_index = -1) const;

  /*
   * Returns the stream index of the specified stream
   * 
   * @param[in] spec    FFmpeg stream specifier string
   * 
   * @return  the non-negative stream number in case of success,
   *          AVERROR_STREAM_NOT_FOUND if no stream with the requested type
   *          could be found,
   *          AVERROR_DECODER_NOT_FOUND if streams were found but no decoder
  */
  int getStreamIndex(const std::string &spec) const;

  /*
   * Returns the frame rate of the "best" video stream.
   * 
   * @param wanted_stream_nb  user-requested stream number,
   *                          or -1 for automatic selection (default)
   * @param[in] get_avg True (default) to retrieve average frame rate. If false,
   *                    it returns r_frame_rate instead.
   * 
   * @return  the non-negative stream number in case of success,
   *          AVERROR_STREAM_NOT_FOUND if no stream with the requested type
   *          could be found,
   *          AVERROR_DECODER_NOT_FOUND if streams were found but no decoder
   */
  double getVideoFrameRate(int wanted_stream_index = -1, const bool get_avg = true) const;

  /*
   * Returns the frame rate of the specified stream
   * 
   * @param[in] spec    FFmpeg stream specifier string
   * @param[in] get_avg True (default) to retrieve average frame rate. If false,
   *                    it returns r_frame_rate instead.
   * 
   * @return  the non-negative stream number in case of success,
   *          AVERROR_STREAM_NOT_FOUND if no stream with the requested type
   *          could be found,
   *          AVERROR_DECODER_NOT_FOUND if streams were found but no decoder
   */
  double getVideoFrameRate(const std::string &spec, const bool get_avg = true) const;

  static mxArray *createMxInfoStruct(mwSize size = 1);
  void dumpToMatlab(mxArray *mxInfo, const int index = 0) const;

private:
  static const char *field_names[10];
  static const char *chapter_field_names[3];
  static const char *program_field_names[4];
  static mxArray *createMxChapterStruct(mwSize size);
  static mxArray *createMxProgramStruct(mwSize size);
};
