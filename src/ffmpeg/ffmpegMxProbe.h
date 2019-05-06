#pragma once

#include <vector>
#include <string>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <mex.h>

#include "ffmpeg_utils.h"

/*
* Standalone class to open a media file to probe its content from Matlab
*/
class FFmpegMxProbe
{
  std::string filename;
  AVFormatContext *fmt_ctx;

  // std::vector<FFmpegInputStream> streams;
  std::vector<AVCodecContextUniquePtr> st_dec_ctx;

public:
  FFmpegMxProbe(const char *filename = nullptr) : fmt_ctx(nullptr)
  {
    if (filename)
      open(filename);
  }
  FFmpegMxProbe(const FFmpegMxProbe &) = delete;            // non construction-copyable
  FFmpegMxProbe(FFmpegMxProbe &&src) : fmt_ctx(src.fmt_ctx) // move xtor
  {
    src.fmt_ctx = nullptr;
    st_dec_ctx = std::move(src.st_dec_ctx);
  }

  FFmpegMxProbe &operator=(const FFmpegMxProbe &) = delete; // non copyable
  FFmpegMxProbe &operator=(FFmpegMxProbe &&src) = delete;   // non movable

  ~FFmpegMxProbe() { close(); }

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

  /*
   * Returns the sample rate of the "best" audio stream.
   * 
   * @param wanted_stream_nb  user-requested stream number,
   *                          or -1 for automatic selection (default)
   * 
   * @return  the non-negative stream number in case of success,
   *          AVERROR_STREAM_NOT_FOUND if no stream with the requested type
   *          could be found,
   *          AVERROR_DECODER_NOT_FOUND if streams were found but no decoder
   */
  int getAudioSampleRate(int wanted_stream_index = -1) const;

  /*
   * Returns the sample rate of the specified audio stream
   * 
   * @param[in] spec    FFmpeg stream specifier string
   * 
   * @return  the non-negative stream number in case of success,
   *          AVERROR_STREAM_NOT_FOUND if no stream with the requested type
   *          could be found,
   *          AVERROR_DECODER_NOT_FOUND if streams were found but no decoder
   */
  int getAudioSampleRate(const std::string &spec) const;

  static mxArray *createMxInfoStruct(mwSize size = 1);
  void dumpToMatlab(mxArray *mxInfo, const int index = 0) const;

private:
  void open(const char *filename, AVInputFormat *iformat = nullptr, AVDictionary *format_opts = nullptr);
  AVCodecContext *open_stream(AVStream *st, AVDictionary *opts);
  void close();

  void dump_stream_to_matlab(const int sid, mxArray *mxInfo, const int index = 0) const;

  static const char *field_names[11];
  static const char *chapter_field_names[3];
  static const char *program_field_names[4];
  static const char *stream_field_names[38];
  static mxArray *createMxChapterStruct(mwSize size);
  static mxArray *createMxProgramStruct(mwSize size);
  static mxArray *createMxStreamStruct(mwSize size);
};
