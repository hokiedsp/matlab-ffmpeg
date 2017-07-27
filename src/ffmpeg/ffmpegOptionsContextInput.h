#pragma once

#include "ffmpegOptionsContext.h"

namespace ffmpeg
{

void define_input_options(OptionDefs &options);

struct InputOptionsContext : public OptionsContext
{
   InputOptionsContext(OptionDefs &all_defs, const int flags=OPT_INPUT) : OptionsContext(all_defs, flags),file_iformat(NULL) {}
   AVCodec *choose_decoder(AVFormatContext *s, AVStream *st) const;

   AVInputFormat *file_iformat;

 protected:
   virtual void parse(const OptionGroup &g);
};
}
