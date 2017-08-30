extern "C" {
}
#include <algorithm>
#include <tuple>
#include <mex.h>
#include "ffmpegFileDump.h"
#include "../Common/mexClassHandler.h" // for mexGetString()

using namespace ffmpeg;

std::string getFileName(const mxArray *arrayFile);
mxArray *setFileFormats(FileDump &info); // formats = setFileFormats();
std::tuple<int, int, int> countStreams(FileDump::Streams_t &streams);
void setVideoStreamFormat(mxArray *streamArray, int index, FileDump::Stream_s &info);
void setAudioStreamFormat(mxArray *streamArray, int index, FileDump::Stream_s &info);
void setSubtitleStreamFormat(mxArray *streamArray, int index, FileDump::Stream_s &info);
void setCommonStreamFormat(mxArray *streamArray, int index, FileDump::Stream_s &info);
mxArray *setChapters(FileDump::Chapters_t &ch);
mxArray *setPrograms(FileDump::Programs_t &prog);
mxArray *setMetaData(FileDump::MetaData_t &info);

// INFO = ffmpegfileinfo(FILENAME)
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  const std::string component_id("ffmpegfileinfo:");

  if (nrhs != 1)
    mexErrMsgIdAndTxt((component_id + "invalidArgument").c_str(), "Takes only 1 input.");
  if (nlhs > 1)
    mexErrMsgIdAndTxt((component_id + "invalidArgument").c_str(), "Produces only 1 output.");

  // make sure the full path is given
  std::string FILENAME;
  try
  {
    FILENAME = getFileName(prhs[0]);
    mexPrintf("filename: %s\n", FILENAME.c_str());
  }
  catch (std::exception &e)
  {
    mexErrMsgIdAndTxt((component_id + "invalidArgument").c_str(), "Invalid Argument: %s", e.what());
  }

  try
  {
    FileDump ffmpeginfo(FILENAME);
    plhs[0] = setFileFormats(ffmpeginfo);
  }
  catch (std::exception &e)
  {
    mexErrMsgIdAndTxt((component_id + "infoExtractionError").c_str(), e.what());
  }
}

std::string getFileName(const mxArray *arrayFile)
{
  mxArray *ME;

  // call Matlab's exist function first
  mxArray *existInArgs[2] = {(mxArray *)arrayFile, mxCreateString("file")};
  mxArray *existOutArg = NULL;
  ME = mexCallMATLABWithTrap(1, &existOutArg, 2, existInArgs, "exist");
  if (ME) // will fail here (in matlab?) if filename is not a string
    throw std::runtime_error("Filename must be a string scalar or character vector.");
  bool fileExists = 2 == (int)mxGetScalar(existOutArg);
  mxDestroyArray(existInArgs[1]);
  if (!fileExists) 
  {
    std::string FILENAME = mexGetString(arrayFile);
    FILENAME.insert(0, 1, '\'');
    throw std::runtime_error((FILENAME + "' not found.").c_str());
  }

  // if only the filename is given, get the full path
  mxArray *whichOutArg = NULL;
  ME = mexCallMATLABWithTrap(1, &whichOutArg, 1, &(mxArray *)arrayFile, "which");
  if (ME)
    throw std::runtime_error(mexGetString(mxGetProperty(ME, 0, "message")).c_str());

  std::string filepath;
  try
  {
    if (mxIsEmpty(whichOutArg)) // path already given
      filepath = mexGetString(arrayFile);
    else // given only filename, return the retrieved full path
      filepath = mexGetString(whichOutArg);
  }
  catch (...)
  {
    throw std::runtime_error("Filename must be a string scalar or character vector.");
  }
  mxDestroyArray(whichOutArg);
  return filepath;
}

#define mxCreateDoubleScalarIfSet(src) (src<0)?mxCreateDoubleMatrix(0,0,mxREAL):mxCreateDoubleScalar(src)

mxArray *setFileFormats(ffmpeg::FileDump &info) // formats = setFileFormats();
{
  //   Filename: 'dc122909 1mo post.mpg'
  //   Path: 'D:\Users\TakeshiIkuma\Documents\Research\clinicdata_conversion'
  //   Duration: 73.654
  //   Audio: [1×1 struct]
  //   Video: [1×1 struct]
  // std::string URL;
  // std::string Format;
  // double Duration;
  // double StartTime;
  // int64_t BitRate;
  // Chapters_t Chapters;
  // Programs_t Programs;
  // Streams_t Streams;
  // MetaData_t MetaData;

  const char *fields[] = {"Filename", "Path", "Duration", "StartTime",
                          "Format", "BitRate", "Video", "Audio",
                          "Subtitle", "Chapters", "Programs", "MetaData"};
  mxArray *infoArray = mxCreateStructMatrix(1, 1, 12, fields);

  const char kPathSeparator =
#ifdef _WIN32
      '\\';
#else
      '/';
#endif
  auto pos = info.URL.find_last_of(kPathSeparator);
  bool has_path = pos != std::string::npos;

  if (has_path) // not found
  {
    mxSetField(infoArray, 0, "Filename", mxCreateString(info.URL.substr(pos + 1).c_str()));
    mxSetField(infoArray, 0, "Path", mxCreateString(info.URL.substr(0, pos).c_str()));
  }
  else
  {
    mxSetField(infoArray, 0, "Filename", mxCreateString(info.URL.c_str()));
    mxSetField(infoArray, 0, "Path", mxCreateString(""));
  }

  mxSetField(infoArray, 0, "Duration", mxCreateDoubleScalarIfSet(info.Duration));
  mxSetField(infoArray, 0, "StartTime", mxCreateDoubleScalarIfSet(info.StartTime));
  mxSetField(infoArray, 0, "Format", mxCreateString(info.Format.c_str()));
  mxSetField(infoArray, 0, "BitRate", mxCreateDoubleScalarIfSet((double)info.BitRate));

  // get number of streams (video,audio,subtitle)
  auto stream_count = countStreams(info.Streams);

#define common_fields_pre "ID", "Type", "CodecName", "CodecTag", "CodecProfile"                // 5 fields
#define common_fields_post "BitRate", "MaximumBitRate", "Language", "Dispositions", "MetaData" // 5 fields
#define num_common_fields 10

  const char *video_fields[] = {common_fields_pre,
                                "ReferenceFrames", "PixelFormat", "ColorRange", "ColorSpace",
                                "ColorPrimaries", "ColorTransfer", "FieldOrder", "ChromaSampleLocation",
                                "Width", "Height", "CodedWidth", "CodedHeight",
                                "SAR", "DAR", "ClosedCaption", "Lossless",
                                "AverageFrameRate", "RealBaseFrameRate", "TimeBase", "CodecTimeBase", 
                                "BitsPerRawSample", // 21
                                common_fields_post};

  mxArray *streamVideoArray = mxCreateStructMatrix(std::get<0>(stream_count), 1, 21+num_common_fields, video_fields);
  mxSetField(infoArray, 0, "Video", streamVideoArray);

  const char *audio_fields[] = {common_fields_pre, 
                                "SampleRate", "ChannelLayout", "SampleFormat", "BitsPerRawSample", "InitialPadding",
                                "TrailingPadding",   // 6
                                common_fields_post}; // excluding SideData
  mxArray *streamAudioArray = mxCreateStructMatrix(std::get<1>(stream_count), 1, 6+num_common_fields, audio_fields);
  mxSetField(infoArray, 0, "Audio", streamAudioArray);

  const char *subtitle_fields[] = {common_fields_pre,   //
                                   "Width", "Height",   // 2
                                   common_fields_post}; // excluding SideData
  mxArray *streamSubtitleArray = mxCreateStructMatrix(std::get<2>(stream_count), 1, 2+num_common_fields, subtitle_fields);
  mxSetField(infoArray, 0, "Subtitle", streamSubtitleArray);

  stream_count = std::make_tuple(0, 0, 0);
  for (auto st = info.Streams.begin(); st < info.Streams.end(); st++)
  {
    if (st->Type == "video")
      setVideoStreamFormat(streamVideoArray, std::get<0>(stream_count)++, *st);
    else if (st->Type == "audio")
      setAudioStreamFormat(streamAudioArray, std::get<1>(stream_count)++, *st);
    else if (st->Type == "subtitle")
      setSubtitleStreamFormat(streamSubtitleArray, std::get<2>(stream_count)++, *st);
  }

  mxSetField(infoArray, 0, "Chapters", setChapters(info.Chapters));
  mxSetField(infoArray, 0, "Programs", setPrograms(info.Programs));
  mxSetField(infoArray, 0, "MetaData", setMetaData(info.MetaData));

  return infoArray;
}

std::tuple<int, int, int> countStreams(FileDump::Streams_t &streams)
{
  std::tuple<int, int, int> rval = std::make_tuple(0, 0, 0);
  for (auto it = streams.begin(); it != streams.end(); it++)
  {
    if (it->Type == "video")
      std::get<0>(rval)++;
    else if (it->Type == "audio")
      std::get<1>(rval)++;
    else if (it->Type == "subtitle")
      std::get<2>(rval)++;
  }

  return rval;
}

void setCommonStreamFormat(mxArray *streamArray, int index, FileDump::Stream_s &st)
{
  mxSetField(streamArray, index, "ID", mxCreateDoubleScalarIfSet(st.ID));
  mxSetField(streamArray, index, "Type", mxCreateString(st.Type.c_str()));
  mxSetField(streamArray, index, "CodecName", mxCreateString(st.CodecName.c_str()));
  mxSetField(streamArray, index, "CodecTag", mxCreateString(st.CodecTag.c_str()));
  mxSetField(streamArray, index, "CodecProfile", mxCreateString(st.CodecProfile.c_str()));
  mxSetField(streamArray, index, "BitRate", mxCreateDoubleScalarIfSet((double)st.BitRate));
  mxSetField(streamArray, index, "MaximumBitRate", mxCreateDoubleScalarIfSet((double)st.MaximumBitRate));
  mxSetField(streamArray, index, "Language", mxCreateString(st.Language.c_str()));

  int dispo = st.Dispositions.Default;
  if (st.Dispositions.Dub) dispo++;
  if (st.Dispositions.Original) dispo++;
  if (st.Dispositions.Comment) dispo++;
  if (st.Dispositions.Lyrics) dispo++;
  if (st.Dispositions.Karaoke) dispo++;
  if (st.Dispositions.Forced) dispo++;
  if (st.Dispositions.HearingImpaired) dispo++;
  if (st.Dispositions.VisualImpaired) dispo++;
  if (st.Dispositions.CleanEffects) dispo++;
  mxArray *dispoArray = mxCreateCellMatrix(1,dispo);
  dispo = 0;
  if (st.Dispositions.Default) mxSetCell(dispoArray,dispo++,mxCreateString("default"));
  if (st.Dispositions.Dub) mxSetCell(dispoArray,dispo++,mxCreateString("dub"));
  if (st.Dispositions.Original) mxSetCell(dispoArray,dispo++,mxCreateString("original"));
  if (st.Dispositions.Comment) mxSetCell(dispoArray,dispo++,mxCreateString("comment"));
  if (st.Dispositions.Lyrics) mxSetCell(dispoArray,dispo++,mxCreateString("lyrics"));
  if (st.Dispositions.Karaoke) mxSetCell(dispoArray,dispo++,mxCreateString("karaoke"));
  if (st.Dispositions.Forced) mxSetCell(dispoArray,dispo++,mxCreateString("forced"));
  if (st.Dispositions.HearingImpaired) mxSetCell(dispoArray,dispo++,mxCreateString("hearing_impaired"));
  if (st.Dispositions.VisualImpaired) mxSetCell(dispoArray,dispo++,mxCreateString("visual_impaired"));
  if (st.Dispositions.CleanEffects) mxSetCell(dispoArray,dispo++,mxCreateString("clean_effects"));
  mxSetField(streamArray, index, "Dispositions", dispoArray);
  
  mxSetField(streamArray, index, "MetaData", setMetaData(st.MetaData));
}

void setVideoStreamFormat(mxArray *streamArray, int index, FileDump::Stream_s &st)
{
  setCommonStreamFormat(streamArray, index, st);

  mxSetField(streamArray, index, "ReferenceFrames", mxCreateDoubleScalarIfSet(st.ReferenceFrames));
  mxSetField(streamArray, index, "PixelFormat", mxCreateString(st.PixelFormat.c_str()));
  mxSetField(streamArray, index, "ColorRange", mxCreateString(st.ColorRange.c_str()));
  mxSetField(streamArray, index, "ColorSpace", mxCreateString(st.ColorSpace.c_str()));
  mxSetField(streamArray, index, "ColorPrimaries", mxCreateString(st.ColorPrimaries.c_str()));
  mxSetField(streamArray, index, "ColorTransfer", mxCreateString(st.ColorTransfer.c_str()));
  mxSetField(streamArray, index, "FieldOrder", mxCreateString(st.FieldOrder.c_str()));
  mxSetField(streamArray, index, "ChromaSampleLocation", mxCreateString(st.ChromaSampleLocation.c_str()));
  mxSetField(streamArray, index, "Width", mxCreateDoubleScalarIfSet(st.Width));
  mxSetField(streamArray, index, "Height", mxCreateDoubleScalarIfSet(st.Height));
  mxSetField(streamArray, index, "CodedWidth", mxCreateDoubleScalarIfSet(st.CodedWidth));
  mxSetField(streamArray, index, "CodedHeight", mxCreateDoubleScalarIfSet(st.CodedHeight));

  mxArray *ratioArray;
  if (st.SAR.first > 0)
  {
    ratioArray = mxCreateDoubleMatrix(1, 2, mxREAL);
    mxGetPr(ratioArray)[0] = st.SAR.first;
    mxGetPr(ratioArray)[1] = st.SAR.second;
  }
  else
  {
    ratioArray = mxCreateDoubleMatrix(0, 0, mxREAL);
  }
  mxSetField(streamArray, index, "SAR", ratioArray);

  if (st.DAR.first > 0)
  {
    ratioArray = mxCreateDoubleMatrix(1, 2, mxREAL);
    mxGetPr(ratioArray)[0] = st.DAR.first;
    mxGetPr(ratioArray)[1] = st.DAR.second;
  }
  else
  {
    ratioArray = mxCreateDoubleMatrix(0, 0, mxREAL);
  }
  mxSetField(streamArray, index, "DAR", ratioArray);

  mxSetField(streamArray, index, "ClosedCaption", mxCreateLogicalScalar(st.ClosedCaption > 0));
  mxSetField(streamArray, index, "Lossless", mxCreateLogicalScalar(st.Lossless > 0));
  mxSetField(streamArray, index, "AverageFrameRate", mxCreateDoubleScalarIfSet(st.AverageFrameRate));
  mxSetField(streamArray, index, "RealBaseFrameRate", mxCreateDoubleScalarIfSet(st.RealBaseFrameRate));
  mxSetField(streamArray, index, "TimeBase", mxCreateDoubleScalarIfSet(st.TimeBase));
  mxSetField(streamArray, index, "CodecTimeBase", mxCreateDoubleScalarIfSet(st.CodecTimeBase));
  mxSetField(streamArray, index, "BitsPerRawSample", mxCreateDoubleScalarIfSet(st.BitsPerRawSample));
}

void setAudioStreamFormat(mxArray *streamArray, int index, FileDump::Stream_s &st)
{
  setCommonStreamFormat(streamArray, index, st);

  mxSetField(streamArray, index, "SampleRate", mxCreateDoubleScalarIfSet(st.SampleRate));
  mxSetField(streamArray, index, "ChannelLayout", mxCreateString(st.ChannelLayout.c_str()));
  mxSetField(streamArray, index, "SampleFormat", mxCreateString(st.SampleFormat.c_str()));
  mxSetField(streamArray, index, "BitsPerRawSample", mxCreateDoubleScalarIfSet(st.BitsPerRawSample));
  mxSetField(streamArray, index, "InitialPadding", mxCreateDoubleScalarIfSet(st.InitialPadding));
  mxSetField(streamArray, index, "TrailingPadding", mxCreateDoubleScalarIfSet(st.TrailingPadding));
}

void setSubtitleStreamFormat(mxArray *streamArray, int index, FileDump::Stream_s &st)
{
  setCommonStreamFormat(streamArray, index, st);
  mxSetField(streamArray, index, "Width", mxCreateDoubleScalarIfSet(st.Width));
  mxSetField(streamArray, index, "Height", mxCreateDoubleScalarIfSet(st.Height));
}

mxArray *setChapters(FileDump::Chapters_t &chs)
{
  const char *ch_fields[] = {"StartTime", "EndTime", "MetaData"};
  mxArray *chArray = mxCreateStructMatrix(chs.size(), 1, 3, ch_fields);

  for (int i = 0; i < chs.size(); i++)
  {
    mxSetField(chArray, i, "StartTime", mxCreateDoubleScalarIfSet(chs[i].StartTime));
    mxSetField(chArray, i, "EndTime", mxCreateDoubleScalarIfSet(chs[i].EndTime));
    mxSetField(chArray, i, "MetaData", setMetaData(chs[i].MetaData));
  }

  return chArray;
}

mxArray *setPrograms(FileDump::Programs_t &prog)
{
  const char *prog_fields[] = {"ID", "Name", "StreamIndices", "MetaData"};
  mxArray *progArray = mxCreateStructMatrix(prog.size(), 1, 3, prog_fields);

  for (int i = 0; i < prog.size(); i++)
  {
    mxSetField(progArray, i, "ID", mxCreateDoubleScalarIfSet(prog[i].ID + 1));
    mxSetField(progArray, i, "Name", mxCreateString(prog[i].Name.c_str()));
    mxArray *sindArray = mxCreateDoubleMatrix(1, prog[i].StreamIndices.size(), mxREAL);
    std::copy(prog[i].StreamIndices.begin(), prog[i].StreamIndices.end(), mxGetPr(sindArray));
    mxSetField(progArray, i, "StreamIndices", sindArray);
    mxSetField(progArray, i, "MetaData", setMetaData(prog[i].MetaData));
  }

  return progArray;
}

mxArray *setMetaData(FileDump::MetaData_t &meta)
{
  // const char *meta_fields[] = {"Key", "Value"};
  mxArray *metaArray = mxCreateCellMatrix(meta.size(), 2); // 2 column cellstr
  for (int i = 0; i < meta.size(); i++)
  {
    mxSetCell(metaArray, i, mxCreateString(meta[i].first.c_str()));
    mxSetCell(metaArray, i + meta.size(), mxCreateString(meta[i].second.c_str()));
  }

  return metaArray;
}
