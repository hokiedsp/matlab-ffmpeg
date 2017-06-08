#pragma once

#include "ffmpegOptionsContext.h"

namespace ffmpeg
{

void define_output_options(OptionDefs &options);

struct OutputOptionsContext : public OptionsContext
{
   OutputOptionsContext(OptionDefs &all_defs, const int flags) : OptionsContext(all_defs, flags),file_oformat(NULL) {}
   AVCodec *choose_encoder(AVFormatContext *s, AVStream *st) const;

   AVOutputFormat *file_oformat;

 protected:
   virtual void parse(const OptionGroup &g);
};
}
