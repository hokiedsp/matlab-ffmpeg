#include <mex.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include <iterator>
#include <regex>

// typedef std::vector<AVCodecID> AVCodecIDs;

// AVCodecIDs get_cids(const AVMediaType type, bool get_encoder)
// {
//   AVCodecIDs rval;
//   for (const AVCodec *codec = av_codec_next(NULL); codec; codec = av_codec_next(codec))
//   {
//     if (codec->type == type &&
//         ((get_encoder && av_codec_is_encoder(codec)) ||
//          (!get_encoder && av_codec_is_decoder(codec))))
//       rval.push_back(codec->id);
//   }
//   return rval;
// }

typedef std::vector<const AVOutputFormat *> AVOutputFormatPtrs;
AVOutputFormatPtrs get_output_formats_devices(const AVMediaType type, const int flags)
{
  AVOutputFormatPtrs rval;
  //AVCodecIDs cids = get_cids(type, true);

  for (AVOutputFormat *ofmt = av_oformat_next(NULL); ofmt; ofmt = av_oformat_next(ofmt))
  {
    // int supported = false;
    // for (AVCodecIDs::iterator cid = cids.begin(); !supported && cid > cids.end(); cid++)
    //   supported = avformat_query_codec(ofmt, *cid, true);
    // if (supported)
    if (((type == AVMEDIA_TYPE_VIDEO && ofmt->video_codec != AV_CODEC_ID_NONE) ||
         (type == AVMEDIA_TYPE_AUDIO && ofmt->audio_codec != AV_CODEC_ID_NONE) ||
         (type == AVMEDIA_TYPE_SUBTITLE && ofmt->subtitle_codec != AV_CODEC_ID_NONE)) &&
        !(ofmt->flags & flags))
      rval.push_back(ofmt);
  }
  return rval;
}

typedef std::vector<const AVInputFormat *> AVInputFormatPtrs;
typedef std::set<std::string> unique_strings;

template <typename FormatPtrs>
unique_strings get_format_names(const FormatPtrs &fmtptrs)
{
  unique_strings rval;
  std::regex comma_re(","); // whitespace
  for (FormatPtrs::const_iterator fmtptr = fmtptrs.begin(); fmtptr < fmtptrs.end(); fmtptr++)
  {
    std::string name = (*fmtptr)->name; // comma separated list of names of output container
    for(std::sregex_token_iterator it(name.begin(), name.end(), comma_re, -1); it!=std::sregex_token_iterator(); it++)
      rval.insert(*it);
  }
  return rval;
}

bool match_format_name(std::string name, const unique_strings &names)
{
  std::regex comma_re(","); // whitespace
  for (std::sregex_token_iterator it(name.begin(), name.end(), comma_re, -1);
       it != std::sregex_token_iterator();
       it++)
    if (names.find(*it) != names.cend())
      return true;
  return false;
}

AVInputFormatPtrs get_input_formats_devices(const AVMediaType type, const int flags)
{
  AVInputFormatPtrs rval;
  AVOutputFormatPtrs ofmtptrs = get_output_formats_devices(type,flags);
  unique_strings ofmt_names = get_format_names(ofmtptrs);

  for (AVInputFormat *ifmt = av_iformat_next(NULL); ifmt; ifmt = av_iformat_next(ifmt))
  {
    bool supported = match_format_name(ifmt->name,ofmt_names);
    
    if (supported)
      rval.push_back(ifmt);
  }
  return rval;
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  if (nlhs>1 || nrhs!=0) mexErrMsgTxt("Takes no argument and returns one variable.");

  av_register_all(); // Register all the muxers, demuxers and codecs.

  AVInputFormatPtrs ifmtptrs = get_input_formats_devices(AVMEDIA_TYPE_VIDEO, AVFMT_NOTIMESTAMPS);

  const char *fields[] = {"name", "long_name", "extensions", "mime_type",
                          "is_file", "need_number", "show_ids", "generic_index",
                          "ts_discont", "bin_search", "gen_search",
                          "byte_seek", "seek_to_pts"};
  plhs[0] = mxCreateStructMatrix(ifmtptrs.size(), 1, 13, fields);

  for (int index = 0; index < ifmtptrs.size(); index++)
  {
    mxSetField(plhs[0], index, "name", mxCreateString(ifmtptrs[index]->name));
    mxSetField(plhs[0], index, "long_name", mxCreateString(ifmtptrs[index]->long_name));
    mxSetField(plhs[0], index, "extensions", mxCreateString(ifmtptrs[index]->extensions));
    mxSetField(plhs[0], index, "mime_type", mxCreateString(ifmtptrs[index]->mime_type));
    mxSetField(plhs[0], index, "is_file", mxCreateLogicalScalar(ifmtptrs[index]->flags&AVFMT_NOFILE));
    mxSetField(plhs[0], index, "need_number", mxCreateLogicalScalar(ifmtptrs[index]->flags&AVFMT_NEEDNUMBER));
    mxSetField(plhs[0], index, "show_ids", mxCreateLogicalScalar(ifmtptrs[index]->flags&AVFMT_SHOW_IDS));
    mxSetField(plhs[0], index, "generic_index", mxCreateLogicalScalar(ifmtptrs[index]->flags&AVFMT_GENERIC_INDEX));
    mxSetField(plhs[0], index, "ts_discont", mxCreateLogicalScalar(ifmtptrs[index]->flags&AVFMT_TS_DISCONT));
    mxSetField(plhs[0], index, "bin_search", mxCreateLogicalScalar(!(ifmtptrs[index]->flags&AVFMT_NOBINSEARCH)));
    mxSetField(plhs[0], index, "gen_search", mxCreateLogicalScalar(!(ifmtptrs[index]->flags&AVFMT_NOGENSEARCH)));
    mxSetField(plhs[0], index, "byte_seek", mxCreateLogicalScalar(ifmtptrs[index]->flags&AVFMT_NO_BYTE_SEEK));
    mxSetField(plhs[0], index, "seek_to_pts", mxCreateLogicalScalar(ifmtptrs[index]->flags&AVFMT_SEEK_TO_PTS));
  }
}
