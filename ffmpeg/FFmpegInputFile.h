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

  FFmpegInputFile(const char *filename) : fmt_ctx(nullptr)
  {
    if (filename)
      open(filename);
  }
  ~FFmpegInputFile();

  void open(const char *filename, AVInputFormat *iformat = nullptr, AVDictionary *format_opts = nullptr);

  std::vector<std::string> getMediaTypes() const;

  static mxArray *createMxInfoStruct(mwSize size = 1);
  void dumpToMatlab(mxArray *mxInfo, const int index = 0) const;

private:
  static const char *field_names[10];
  static const char *chapter_field_names[3];
  static const char *program_field_names[4];
  static mxArray *createMxChapterStruct(mwSize size);
  static mxArray *createMxProgramStruct(mwSize size);
};
