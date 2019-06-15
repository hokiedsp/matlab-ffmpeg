#pragma once

#include <string>

extern "C"
{
// #include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
  // #include <libavutil/pixdesc.h>
}

namespace ffmpeg
{

/*
 * @param[in] st         pointer to the stream to dump
 * @param[in] is_output  true if output stream
 * @param[in] flags      either ic->oformat->flags or ic->iformat->flags where
 *                       ic is the AVFormatContext pointer.
 * @param[in] separator  
 */
std::string dumpStreamFormat(AVStream *st, bool is_output, int flags, char *separator);
} // namespace ffmpeg
