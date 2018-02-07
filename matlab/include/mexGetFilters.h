#pragma once
#include <mex.h>

extern "C" {
#include <libavfilter/avfilter.h>
}

template <class UnaryPredicate>
void getFilters(int nlhs, mxArray *plhs[], UnaryPredicate pred) // formats = getVideoFormats();
{
  // build a list of pixel format descriptors
  std::vector<const AVFilter *> filters;
  filters.reserve(256);
  for (const AVFilter *filter = avfilter_next(NULL);
       filter != NULL;
       filter = avfilter_next(filter))
    if (pred(filter))
      filters.push_back(filter);

  std::sort(filters.begin(), filters.end(),
            [](const AVFilter *a, const AVFilter *b) -> bool { return strcmp(a->name, b->name) < 0; });

  const int nfields = 11;
  const char *fieldnames[11] = {
      "Name", "Description", "InputType", "NumberOfVideoInputs", "NumberOfAudioInputs",
      "OutputType", "NumberOfVideoOutputs", "NumberOfAudioOutputs", "CommandInput", "TimelineSupport", "Multithreaded"};

  plhs[0] = mxCreateStructMatrix(filters.size(), 1, nfields, fieldnames);

  for (int j = 0; j < filters.size(); ++j)
  {
    const AVFilter *filter = filters[j];

    mxSetField(plhs[0], j, "Name", mxCreateString(filter->name));
    mxSetField(plhs[0], j, "Description", mxCreateString(filter->description));
    mxSetField(plhs[0], j, "CommandInput", mxCreateString((filter->process_command) ? "on" : "off"));
    mxSetField(plhs[0], j, "TimelineSupport", mxCreateString((filter->flags & AVFILTER_FLAG_SUPPORT_TIMELINE) ? "on" : "off"));
    mxSetField(plhs[0], j, "Multithreaded", mxCreateString((filter->flags & AVFILTER_FLAG_SLICE_THREADS) ? "on" : "off"));

    const std::string iostr[2] = {"Input", "Output"};
    for (int i = 0; i < 2; i++)
    {
      int nvideo = 0, naudio = 0;

      const AVFilterPad *pad = i ? filter->outputs : filter->inputs;
      for (int k = 0; pad && avfilter_pad_get_name(pad, k); ++k)
      {
        switch (avfilter_pad_get_type(pad, k))
        {
        case AVMEDIA_TYPE_VIDEO:
          ++nvideo;
          break;
        case AVMEDIA_TYPE_AUDIO:
          ++naudio;
        }
      }
      std::string type_field = iostr[i] + "Type";
      if (nvideo)
      {
        if (naudio)
          mxSetField(plhs[0], j, type_field.c_str(), mxCreateString("mixed"));
        else
          mxSetField(plhs[0], j, type_field.c_str(), mxCreateString("video"));
      }
      else if (naudio)
          mxSetField(plhs[0], j, type_field.c_str(), mxCreateString("audio"));
      else
          mxSetField(plhs[0], j, type_field.c_str(), mxCreateString("unspecified"));
      
      bool dyn = (!i && (filter->flags & AVFILTER_FLAG_DYNAMIC_INPUTS)) ||
                          (i && (filter->flags & AVFILTER_FLAG_DYNAMIC_OUTPUTS));
      if (dyn && !naudio) naudio = -1;
      if (dyn && !nvideo) nvideo = -1;
    
      const std::string num = "NumberOf";
      mxSetField(plhs[0], j, (num+"Video"+iostr[i]+'s').c_str(), mxCreateDoubleScalar(nvideo));
      mxSetField(plhs[0], j, (num+"Audio"+iostr[i]+'s').c_str(), mxCreateDoubleScalar(naudio));
    }
  }
}

inline void getFilters(int nlhs, mxArray *plhs[])
{
  getFilters(nlhs, plhs, [](const AVFilter*) { return true; });
}
