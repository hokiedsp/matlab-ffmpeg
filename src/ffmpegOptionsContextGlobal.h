#pragma once

#include "ffmpegOptionsContext.h"

namespace ffmpeg
{

void define_input_options(OptionDefs &options);

struct GlobalOptionsContext : public OptionsContext // also inherits AvOptionGroup & ffmpegBase
{
   GlobalOptionsContext(OptionDefs &all_defs, const int flags) : OptionsContext(all_defs, flags) {}

 protected:
   virtual void parse(const OptionGroup &g);
};
}
