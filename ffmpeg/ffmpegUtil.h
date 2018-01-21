#pragma once

#include <string>

extern "C" {
#include <libavcodec/avcodec.h> // AVCodec
#include <libavutil/avutil.h>   // AVMediaType
}

#include "ffmpegPtrs.h"

namespace ffmpeg
{
AVCodec *find_encoder(const std::string &name, const AVMediaType type);
AVCodec *find_decoder(const std::string &name, const AVMediaType type);

// remove entries from Dictionary A, which matches entries in Dictionary B
void remove_avoptions(AVDictionary *&a, AVDictionary *b);

// check to make sure dictionary is consumed???
void assert_avoptions(AVDictionary *m);

DictPtr strip_specifiers(AVDictionary *dict);

AVRational duration_max(int64_t tmp, int64_t &duration, AVRational tmp_time_base, AVRational time_base);

// void dump_attachment(AVStream *st, const std::string &filename);
// void assert_file_overwrite(const std::string &filename);
}
