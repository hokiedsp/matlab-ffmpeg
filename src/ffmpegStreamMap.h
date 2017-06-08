#pragma once

#include <string>
#include <vector>

extern "C" {
#include <libavfilter/avfilter.h> // for AVFilterContext
#include <libavutil/avutil.h>     // for AVMediaType
}

#include "ffmpegBase.h"

namespace ffmpeg
{

/* select an input stream for an output stream */
struct StreamMap
{
   int disabled; /* 1 is this mapping is disabled by a negative map */
   int file_index;
   int stream_index;
   int sync_file_index;
   int sync_stream_index;
   char *linklabel; /* name of an output link, for mapping lavfi outputs */
};

}
