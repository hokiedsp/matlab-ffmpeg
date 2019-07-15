#include <mex.h>

#include "../ffmpeg/ffmpegReader.h"

#include "../ffmpeg/ffmpegTimeUtil.h"
#include "../ffmpeg/mxutils.h"

#include <mexGetString.h>

extern "C"
{
#include <libavutil/dict.h>
}

#include <algorithm>

#include <chrono>
typedef std::chrono::duration<double> mex_duration_t;

bool log_uninit = true;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  // initialize ffmpeg::Exception
  if (log_uninit)
  {
    ffmpeg::Exception::initialize();
    // ffmpeg::Exception::log_fcn = [](const auto &msg) {
    //   mexPrintf(msg.c_str());
    // };
    log_uninit = false;
  }

  if (nlhs > 1 || nrhs != 1)
    mexErrMsgIdAndTxt("ffmpeg.audioinfo:invalidNumberOfArguments",
                      "Invalid number of input or output arguments specified.");

  // get full path to the audio file
  mxArray *mxURL;
  mexCallMATLAB(1, &mxURL, 1, (mxArray **)prhs, "which");

  // open the audio file
  ffmpeg::Reader<ffmpeg::AVFrameQueueST> reader;
  reader.openFile(mexGetString(mxURL));

  // add audio stream (throws InvalidStreamSpecifier if no audio stream found)
  int stream_id = reader.addStream(AVMEDIA_TYPE_AUDIO);

  // retrieve info
  const char *fieldnames[] = {
      "Filename",     "StreamId",      "CompressionMethod",
      "NumChannels",  "ChannelLayout", "SampleRate",
      "TotalSamples", "Duration",      "Title",
      "Comment",      "Artist"};
  mxArray *mxStruct = mxCreateStructMatrix(1, 1, 11, fieldnames);
  plhs[0] = mxStruct;

  // set Matlab class properties
  mxSetField(mxStruct, 0, "Filename", mxURL);
  mxSetField(mxStruct, 0, "StreamId", mxCreateDoubleScalar(stream_id));

  ffmpeg::InputAudioStream &stream =
      dynamic_cast<ffmpeg::InputAudioStream &>(reader.getStream(stream_id));
  const AVStream *st = stream.getAVStream();
  if (!st)
    mexErrMsgIdAndTxt("ffmpeg:audioinfo:Unknown",
                      "Could not retrieve AVStream.");
  const AVCodecParameters *codecpar = st->codecpar;
  if (!codecpar)
    mexErrMsgIdAndTxt("ffmpeg:audioinfo:Unknown",
                      "Could not retrieve AVStream.");

  mex_duration_t T =
      ffmpeg::get_timestamp<mex_duration_t>(st->duration, st->time_base);
  mxSetField(mxStruct, 0, "Duration", mxCreateDoubleScalar(T.count()));
  mxSetField(mxStruct, 0, "SampleRate",
             mxCreateDoubleScalar(codecpar->sample_rate));
  mxSetField(
      mxStruct, 0, "TotalSamples",
      mxCreateDoubleScalar(std::round(codecpar->sample_rate * T.count())));

  mxSetField(mxStruct, 0, "CompressionMethod",
             mxCreateString(stream.getCodecName().c_str()));
  mxSetField(mxStruct, 0, "ChannelLayout",
             mxCreateString(stream.getChannelLayoutName().c_str()));
  mxSetField(mxStruct, 0, "NumChannels",
             mxCreateDoubleScalar(codecpar->channels));
  if (codecpar->bit_rate)
    mxSetFieldByNumber(
        mxStruct, 0, mxAddField(mxStruct, "BitRate"),
        mxCreateDoubleScalar((double)codecpar->bit_rate / 1000.0));
  if (codecpar->bits_per_raw_sample)
    mxSetFieldByNumber(mxStruct, 0, mxAddField(mxStruct, "BitsPerSample"),
                       mxCreateDoubleScalar(codecpar->bits_per_raw_sample));

  // get metadata if available)
  const AVDictionary *metadata = reader.getMetadata();
  if (metadata)
  {
    AVDictionaryEntry *field = av_dict_get(metadata, "Title", nullptr, NULL);
    if (field)
      mxSetProperty(mxStruct, 0, "Title", mxCreateString(field->value));
    field = av_dict_get(metadata, "Artist", nullptr, NULL);
    if (field)
      mxSetProperty(mxStruct, 0, "Artist", mxCreateString(field->value));
    field = av_dict_get(metadata, "Comment", nullptr, NULL);
    if (field)
      mxSetProperty(mxStruct, 0, "Comment", mxCreateString(field->value));
  }
}
