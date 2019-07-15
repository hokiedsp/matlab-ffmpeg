#include <mex.h>

#include "../ffmpeg/ffmpegReader.h"

#include "../ffmpeg/ffmpegPtrs.h"
#include "../ffmpeg/ffmpegTimeUtil.h"
#include "../ffmpeg/mxutils.h"
#include "@Reader/mexReaderPostOps.h"

#include <mexGetString.h>

extern "C"
{
#include <libavutil/dict.h>
}

#include <algorithm>

#include <chrono>
typedef std::chrono::duration<double> mex_duration_t;

bool log_uninit = true;

// [Y, FS]=audioread(FILENAME)
// [Y, FS]=audioread(FILENAME, [START END])
// [Y, FS]=audioread(FILENAME, DATATYPE)
// [Y, FS]=audioread(FILENAME, [START END], DATATYPE);

struct InputArgs
{
  std::string url;
  size_t start;
  size_t end;
  AVSampleFormat format;
  mxClassID class_id;

  InputArgs(int nrhs, const mxArray *prhs[]);
};

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  // initialize ffmpeg::Exception
  if (log_uninit)
  {
    ffmpeg::Exception::initialize();
    // ffmpeg::Exception::log_fcn = [](const auto &msg) {
    //   mexPrintf("%s\n", msg.c_str());
    // };
    log_uninit = false;
  }

  if (nlhs > 2 || nrhs < 1 || nrhs > 3)
    mexErrMsgIdAndTxt("ffmpeg.audioinfo:invalidNumberOfArguments",
                      "Invalid number of input or output arguments specified.");

  // parse the input arguments
  InputArgs args(nrhs, prhs);

  // open the audio file
  ffmpeg::Reader<ffmpeg::AVFrameQueueST> reader;
  reader.openFile(args.url);

  // add audio stream (throws InvalidStreamSpecifier if no audio stream found)
  int stream_id = reader.addStream(AVMEDIA_TYPE_AUDIO);
  ffmpeg::InputAudioStream &stream =
      dynamic_cast<ffmpeg::InputAudioStream &>(reader.getStream(stream_id));

  // activate the reader
  reader.activate();

  // set postop filter if needed
  AVSampleFormat format = stream.getFormat();
  if (args.format == AV_SAMPLE_FMT_NONE)
  {
    args.format = av_get_packed_sample_fmt(format);
    switch (args.format)
    {
    case AV_SAMPLE_FMT_U8: args.class_id = mxUINT8_CLASS; break;
    case AV_SAMPLE_FMT_S16: args.class_id = mxINT16_CLASS; break;
    case AV_SAMPLE_FMT_S32: args.class_id = mxINT32_CLASS; break;
    case AV_SAMPLE_FMT_S64: args.class_id = mxINT64_CLASS; break;
    case AV_SAMPLE_FMT_FLT: args.class_id = mxSINGLE_CLASS; break;
    default: args.class_id = mxDOUBLE_CLASS;
    }
  }
  if (args.format != format)
  {
    format = args.format;
    reader.setPostOp<mexFFmpegAudioPostOp>(stream_id, format);
  }

  // analyze time-base & sample rate
  int fs = stream.getSampleRate();
  AVRational tb = stream.getTimeBase();
  bool tbIsSamplePeriod = tb.num == 1 && tb.den == fs;
  AVRational tb2Period = av_mul_q(tb, AVRational({fs, 1}));
  auto get_frame_time = [tbIsSamplePeriod, tb2Period](const AVFrame *frame) {
    return tbIsSamplePeriod ? frame->best_effort_timestamp
                            : av_rescale(frame->best_effort_timestamp,
                                         tb2Period.num, tb2Period.den);
  };

  // set start & end 0-based sample indices
  uint64_t start(0), end(0);
  bool toEOF(!args.end);
  if (args.start) { start = args.start - 1; }
  if (toEOF)
    end = stream.getTotalNumberOfSamples();
  else
    end = args.end;

  // get estimated number of samples to be read
  size_t N = end - start;
  size_t Nch = stream.getChannels();

  mxArray *Yt = mxCreateNumericMatrix(Nch, N, args.class_id, mxREAL);
  uint8_t *data = (uint8_t *)mxGetData(Yt);
  size_t elsz = mxGetElementSize(Yt);

  size_t n_left = N; // bytes left in the buffer
  uint8_t *dst[AV_NUM_DATA_POINTERS];
  dst[0] = data;
  dst[1] = nullptr;

  // number of bytes/sample
  size_t nbuf = elsz * Nch;

  AVFrame *frame = av_frame_alloc();
  ffmpeg::AVFramePtr frame_cleanup(frame, ffmpeg::delete_av_frame);

  auto copy_data = [&dst, &n_left, &Yt, nbuf, format](const AVFrame *frame,
                                                      int n, int offset = 0) {
    if (n_left < n)
    {
      // if run out of space, expand the mxArray
      uint8_t *data = (uint8_t *)mxGetData(Yt);
      size_t elsz = mxGetElementSize(Yt);
      size_t dst_offset = dst[0] - data;
      size_t N = mxGetN(Yt) + (n - n_left);
      data = (uint8_t *)mxRealloc(data, N * frame->channels * elsz);
      mxSetData(Yt, data);
      mxSetN(Yt, N);
      dst[0] = data + dst_offset;
      n_left = 0;
    }
    else
    {
      n_left -= n;
    }
    av_samples_copy(dst, frame->data, 0, offset, n, frame->channels, format);

    dst[0] += nbuf * n;
  };

  // seek to near the starting frame
  reader.seek(mex_duration_t(start / (double)fs), false);

  // get the first frame
  reader.readNextFrame(frame, stream_id);
  if (start > 0)
  {
    // get frames until reader's next frame is past the starting time while
    // the last read frame contains the requested start time
    mex_duration_t t0(start / (double)fs);
    while (!reader.atEndOfStream(stream_id) &&
           reader.getTimeStamp<mex_duration_t>(stream_id) < t0)
    {
      av_frame_unref(frame);
      reader.readNextFrame(frame, stream_id);
    }
  }

  // no data (shouldn't happen)
  if (frame->nb_samples == 0)
    mexErrMsgIdAndTxt("ffmpeg:audioread:NoData", "No data found.");

  // copy the data from the first frame
  int offset = (int)(start - get_frame_time(frame));
  if (offset < 0)
    mexErrMsgIdAndTxt("ffmpeg:audioread:BadOffset", "Seek failed.");

  int n = frame->nb_samples - offset;
  copy_data(frame, (toEOF || n < n_left) ? n : (int)n_left, offset);

  // work the remaining frames
  while (!reader.atEndOfStream(stream_id) && (toEOF || n_left > 0))
  {
    reader.readNextFrame(frame, stream_id);
    n = frame->nb_samples;
    if (!toEOF && n > n_left) n = (int)n_left;
    copy_data(frame, n);
  }

  // call MATLAB transpose function to finalize combined-audio output
  mxArray *Y;
  mexCallMATLAB(1, &Y, 1, &Yt, "transpose");
  mxDestroyArray(Yt);

  plhs[0] = Y;
  if (nlhs > 1) plhs[1] = mxCreateDoubleScalar(fs);
}

// [Y, FS]=audioread(FILENAME)
// [Y, FS]=audioread(FILENAME, [START END])
// [Y, FS]=audioread(FILENAME, DATATYPE)
// [Y, FS]=audioread(FILENAME, [START END], DATATYPE);
InputArgs::InputArgs(int nrhs, const mxArray *prhs[])
    : start(0), end(0), format(AV_SAMPLE_FMT_DBL), class_id(mxDOUBLE_CLASS)
{
  mxArray *mxURL;
  mexCallMATLAB(1, &mxURL, 1, (mxArray **)prhs, "which");
  url = mexGetString(mxURL);
  int arg(1);
  if (nrhs > 1 && !mxIsChar(prhs[1]))
  {
    if (!(mxIsDouble(prhs[1]) && mxGetNumberOfElements(prhs[1]) == 2))
      mexErrMsgIdAndTxt(
          "ffmpeg:audioread:InvalidInputArguments",
          "[START END] vector must exactly contain 2 double elements");
    double *data = mxGetPr(prhs[1]);
    start = (size_t)data[0];
    end = (size_t)data[1];
    if (start != data[0] || end != data[1])
      mexErrMsgIdAndTxt(
          "ffmpeg:audioread:InvalidInputArguments",
          "Expected [START END] input argument to be integer-valued");
    if (start == 0 || end == 0)
      mexErrMsgIdAndTxt("ffmpeg:audioread:InvalidInputArguments",
                        "Expected [START END] input argument to be positive");
    if (start > end)
      mexErrMsgIdAndTxt("ffmpeg:audioread:InvalidInputArguments",
                        "START input argument must be less than or equal to "
                        "END input argument");
    arg = 2;
  }
  if (nrhs > arg)
  {
    if (!(mxIsChar(prhs[arg])))
      mexErrMsgIdAndTxt("ffmpeg:audioread:InvalidInputArguments",
                        "DATATYPE must be character array.");
    std::string mxtype = mexGetString(prhs[arg]);
    if (mxtype == "native") { format = AV_SAMPLE_FMT_NONE; }
    else if (mxtype == "uint8")
    {
      class_id = mxUINT8_CLASS;
      format = AV_SAMPLE_FMT_U8;
    }
    else if (mxtype == "int16")
    {
      class_id = mxINT16_CLASS;
      format = AV_SAMPLE_FMT_S16;
    }
    else if (mxtype == "int32")
    {
      class_id = mxINT32_CLASS;
      format = AV_SAMPLE_FMT_S32;
    }
    else if (mxtype == "int64")
    {
      class_id = mxINT64_CLASS;
      format = AV_SAMPLE_FMT_S64;
    }
    else if (mxtype == "single")
    {
      class_id = mxSINGLE_CLASS;
      format = AV_SAMPLE_FMT_FLT;
    }
    else if (mxtype == "double")
    {
      class_id = mxDOUBLE_CLASS;
      format = AV_SAMPLE_FMT_DBL;
    }
    else
    {
      mexErrMsgIdAndTxt("ffmpeg:audioread:InvalidInputArguments",
                        "Unknown DATATYPE given %s.", mxtype.c_str());
    }
  }
}
