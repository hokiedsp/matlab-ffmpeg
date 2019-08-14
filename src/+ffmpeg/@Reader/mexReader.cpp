#include "mexReader.h"

#include "../../ffmpeg/ffmpegImageUtils.h"
#include "../../ffmpeg/mxutils.h"

#include <mexGetString.h>

extern "C"
{
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>

bool ini = true;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{

  // initialize ffmpeg::Exception
  if (ini)
  {
    ffmpeg::Exception::initialize();
    ffmpeg::Exception::log_fcn = [](const auto &msg) {
      mexPrintf(msg.c_str());
    };
    ini = false;
  }

  mexObjectHandler<mexFFmpegReader>(nlhs, plhs, nrhs, prhs);
}

//////////////////////////////////////////////////////////////////////////////////

// mexFFmpegReader(mobj, filename) (all arguments  pre-validated)
mexFFmpegReader::mexFFmpegReader(const mxArray *mxObj, int nrhs,
                                 const mxArray *prhs[])
{
  // reserve one temp frame
  add_frame();

  // open the video file
  reader.openFile(mexGetString(prhs[0]));
}

mexFFmpegReader::~mexFFmpegReader()
{
  for (auto &frame : frames) av_frame_free(&frame);
}

bool mexFFmpegReader::action_handler(const mxArray *mxObj,
                                     const std::string &command, int nlhs,
                                     mxArray *plhs[], int nrhs,
                                     const mxArray *prhs[])
{
  if (command == "setCurrentTime")
  {
    if (!(mxIsNumeric(prhs[0]) && mxIsScalar(prhs[0])) || mxIsComplex(prhs[0]))
      throw 0;

    // get new time
    setCurrentTime(prhs[0]);
  }
  else if (command == "getCurrentTime")
  {
    plhs[0] = getCurrentTime();
  }
  else if (command == "get_nb_streams")
    plhs[0] = mxCreateDoubleScalar((double)reader.getStreamCount());
  else if (command == "activate")
    activate((mxArray *)mxObj);
  else if (command == "readFrame")
    readFrame(nlhs, plhs, nrhs, prhs);
  else if (command == "read")
    read(nlhs, plhs, nrhs, prhs);
  else if (command == "hasFrame")
    plhs[0] = hasFrame();
  else if (command == "hasVideo")
    plhs[0] = hasMediaType(AVMEDIA_TYPE_VIDEO);
  else if (command == "hasVideo")
    plhs[0] = hasMediaType(AVMEDIA_TYPE_AUDIO);
  return true;
}

bool mexFFmpegReader::static_handler(const std::string &command, int nlhs,
                                     mxArray *plhs[], int nrhs,
                                     const mxArray *prhs[])
{
  if (command == "getFileFormats")
  {
    //   if (nrhs > 0)
    //     throw std::runtime_error("getFileFormats() takes no input
    //     argument.");
    //   mexFFmpegReader::getFileFormats(nlhs, plhs);
    // }
    // else if (command == "getVideoFormats")
    // {
    //   if (nrhs > 0)
    //     throw std::runtime_error("getVideoFormats() takes no input
    //     argument.");
    //   mexFFmpegReader::getVideoFormats(nlhs, plhs);
    // }
    // else if (command == "getVideoCompressions")
    // {
    //   if (nrhs > 0)
    //     throw std::runtime_error("getVideoCompressions() takes no input
    //     argument.");
    //   mexFFmpegReader::getVideoCompressions(nlhs, plhs);
    //   avcodec_find_decoder(desc->id) && desc->type == AVMEDIA_TYPE_VIDEO &&
    //   !strstr(desc->name, "_deprecated")
  }
  else if (command == "validate_pixfmt")
  {
    std::string pixfmt = mexGetString(prhs[0]);
    if (av_get_pix_fmt(pixfmt.c_str()) == AV_PIX_FMT_NONE)
      mexErrMsgIdAndTxt("ffmpeg:Reader:validate_pixfmt:invalidFormat",
                        "%s is not a valid FFmpeg Pixel Format",
                        pixfmt.c_str());
  }
  else if (command == "validate_samplefmt")
  {
    std::string samplefmt = mexGetString(prhs[0]);
    if (av_get_sample_fmt(samplefmt.c_str()) == AV_SAMPLE_FMT_NONE)
      mexErrMsgIdAndTxt("ffmpeg:Reader:validate_samplefmt:invalidFormat",
                        "%s is not a valid FFmpeg Sample Format",
                        samplefmt.c_str());
  }
  else
    return false;
  return true;
}

////////////////////////////////////////////////////////////////////////////////////////

void mexFFmpegReader::setCurrentTime(const mxArray *mxTime)
{
  mex_duration_t time(mxGetScalar(mxTime));
  reader.seek(time);
}

mxArray *mexFFmpegReader::getCurrentTime()
{
  mex_duration_t t = reader.getTimeStamp<mex_duration_t>(streams[0]);
  return mxCreateDoubleScalar(t.count());
}

mxArray *mexFFmpegReader::hasFrame()
{
  return mxCreateLogicalScalar(!reader.atEndOfStream(streams[0]));
}

mxArray *mexFFmpegReader::hasMediaType(const AVMediaType type)
{
  for (auto &spec : streams)
  {
    if (dynamic_cast<ffmpeg::IMediaHandler &>(reader.getStream(spec))
            .getMediaType() == type)
      return mxCreateLogicalScalar(true);
  }
  return mxCreateLogicalScalar(false);
}

//[frame1,frame2,...] = readFrame(obj, varargin);
void mexFFmpegReader::readFrame(int nlhs, mxArray *plhs[], int nrhs,
                                const mxArray *prhs[])
{
  if (!hasFrame())
    mexErrMsgIdAndTxt("ffmpeg:Reader:EndOfFile",
                      "No more frames available to read from file.");

  // read the next frame of primary stream
  plhs[0] = read_frame();

  // for each stream outputs, read the next frame(s) until
  for (int i = 1; i < std::min<size_t>(nlhs, streams.size()); ++i)
    plhs[i] = read_frame(streams[i]);

  // std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  // if (!rd_buf->available()) buffer_ready.wait(buffer_guard);
  // rd_buf->read_frame(dst, (nlhs > 1) ? &t : NULL);
  // buffer_ready.notify_one();
  // buffer_guard.unlock();

  // if (nlhs > 1) plhs[1] = mxCreateDoubleScalar(t);
}

// read frame from the primary stream
mxArray *mexFFmpegReader::read_frame()
{
  // get primary stream specifier string
  const std::string &spec = streams[0];

  // temp frame storage
  AVFrame *frame = frames[0];

  // read next frame for the primary stream (this effectively populates frames
  // buffer)
  bool eof = reader.readNextFrame(frame, spec);

  // if the stream has already reached the end, return an empty matrix
  if (eof) return mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL);

  // automatically unreference frame when exiting this function
  purge_frames purger(frames);

  ffmpeg::IAVFrameSource &src = reader.getStream(spec);
  if (src.getMediaType() == AVMEDIA_TYPE_VIDEO)
    return read_video_frame(1);
  else if (src.getMediaType() == AVMEDIA_TYPE_AUDIO)
    return read_audio_frame(1);
  else
    throw ffmpeg::Exception("Encountered data from an unexpected stream.");
}

// returns mxArray containing the specified secondary stream data
mxArray *mexFFmpegReader::read_frame(const std::string &spec)
{
  // first, get the time stamp of the next primary stream frame
  auto ts = reader.getTimeStamp<mex_duration_t>(streams[0]);

  // automatically unreference frame when exiting this function
  purge_frames purger(frames, 0);

  // read frames with ts less than the next primary stream frame
  bool eof = false;
  while (!eof)
  {
    // secondary frame may not be available
    mex_duration_t t;
    try
    {
      t = reader.getTimeStamp<mex_duration_t>(spec);
    }
    catch (ffmpeg::Exception &) // no frame avail.
    {
      return mxCreateDoubleMatrix(0, 0, mxREAL);
    }
    if (t >= ts) break;

    if (frames.size() <= purger.nfrms) add_frame();
     AVFrame *frame = frames[purger.nfrms];
     eof = reader.readNextFrame(frame, spec);
     if (!eof) ++purger.nfrms;
  }

  ffmpeg::IAVFrameSource &src = reader.getStream(spec);
  if (src.getMediaType() == AVMEDIA_TYPE_VIDEO)
    return read_video_frame(purger.nfrms);
  else if (src.getMediaType() == AVMEDIA_TYPE_AUDIO)
    return read_audio_frame(purger.nfrms);
  else
    throw ffmpeg::Exception("Encountered data from an unexpected stream.");
}

// convert data in the first nframes AVFrames in the frames vector
mxArray *mexFFmpegReader::read_video_frame(size_t nframes)
{
  // could be empty
  if (!nframes) return mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL);

  AVFrame *frame = frames[0];

  // ffmpeg::IVideoHandler &vsrc = dynamic_cast<ffmpeg::IVideoHandler &>(src);
  int width = frame->width;
  int height = frame->height;
  AVPixelFormat format = (AVPixelFormat)frame->format;

  int frame_data_sz =
      ffmpeg::imageGetComponentBufferSize(format, width, height);

  mwSize dims[4] = {(mwSize)width, (mwSize)height,
                    (mwSize)frame_data_sz / (width * height), nframes};
  mxArray *mxData = mxCreateNumericArray(3, dims, mxUINT8_CLASS, mxREAL);
  uint8_t *data = (uint8_t *)mxGetData(mxData);

  for (size_t i = 0; i < nframes; ++i)
  {
    ffmpeg::imageCopyToComponentBuffer(data, frame_data_sz, frame->data,
                                       frame->linesize, format, frame->width,
                                       frame->height);
    data += frame_data_sz;
  }
  return mxData;
}

// convert data in the first nframes AVFrames in the frames vector
mxArray *mexFFmpegReader::read_audio_frame(size_t nframes)
{
  // ffmpeg::IAudioHandler &asrc = dynamic_cast<ffmpeg::IAudioHandler &>(src);

  // could be empty
  if (!nframes) return mxCreateDoubleMatrix(0, 0, mxREAL);

  AVFrame *frame = frames[0];

  AVSampleFormat fmt = (AVSampleFormat)frame->format;

  // just in case...
  if ((bool)av_sample_fmt_is_planar(fmt))
    throw ffmpeg::Exception("Audio frame format is not as expected.");

  mxClassID mx_class;
  switch (fmt)
  {
  case AV_SAMPLE_FMT_U8: ///< unsigned 8 bits
    mx_class = mxUINT8_CLASS;
    break;
  case AV_SAMPLE_FMT_S16: ///< signed 16 bits
    mx_class = mxINT16_CLASS;
    break;
  case AV_SAMPLE_FMT_S32: ///< signed 32 bits
    mx_class = mxINT32_CLASS;
    break;
  case AV_SAMPLE_FMT_FLT: ///< float
    mx_class = mxSINGLE_CLASS;
    break;
  case AV_SAMPLE_FMT_DBL: ///< double
    mx_class = mxDOUBLE_CLASS;
    break;
  case AV_SAMPLE_FMT_S64: ///< signed 64 bits
    mx_class = mxINT64_CLASS;
    break;
  default: throw ffmpeg::Exception("Unknown audio sample format.");
  }

  size_t total_nb_samples = std::reduce(
      frames.begin(), frames.begin() + nframes, 0ull,
      [](size_t N, AVFrame *frame) { return N + frame->nb_samples; });

  mxArray *mxData = mxCreateNumericMatrix(frame->channels, total_nb_samples,
                                          mx_class, mxREAL);
  uint8_t *data = (uint8_t *)mxGetData(mxData);

  auto elsz = mxGetElementSize(mxData);

  uint8_t *dst[AV_NUM_DATA_POINTERS];
  dst[0] = data;
  dst[1] = nullptr;

  for (int j = 0; j < nframes; ++j)
  {
    av_samples_copy(dst, frame->data, 0, 0, frame->nb_samples, frame->channels,
                    fmt);
    dst[0] += av_samples_get_buffer_size(nullptr, frame->channels,
                                         frame->nb_samples, fmt, false);
  }

  // call MATLAB transpose function to finalize combined-audio output
  mxArray *mxDataT;
  mexCallMATLAB(1, &mxDataT, 1, &mxData, "transpose");
  mxDestroyArray(mxData);

  return mxDataT;
}

void mexFFmpegReader::read(
    int nlhs, mxArray *plhs[], int nrhs,
    const mxArray *prhs[]) // varargout = read(obj, varargin);
{
  throw std::runtime_error(
      "Not supported. Use readFrame() or readBuffer() instead.");
}

////////////////////////////////////////////////////////////////////////////////////////////
// Private member functions
// finalize the configuration and activate the reader
void mexFFmpegReader::activate(mxArray *mxObj)
{
  // set filter graph if specified
  filt_desc = mexGetString(mxGetProperty(mxObj, 0, "FilterGraph"));
  if (filt_desc.size()) reader.setFilterGraph(filt_desc);

  // set streams to read based on Streams property value
  set_streams(mxObj);

  // activate the reader (fills all buffers with at least one frame)
  reader.activate();

  // set post-ops
  set_postops(mxObj);

  // set Matlab class properties
  mxArray *mxData;
  mxData = mxCreateCellMatrix(1, streams.size());
  for (int i = 0; i < streams.size(); ++i)
    mxSetCell(mxData, i, mxCreateString(streams[i].c_str()));
  mxSetProperty(mxObj, 0, "Streams", mxData);

  mxSetProperty(
      mxObj, 0, "Duration",
      mxCreateDoubleScalar(reader.getDuration<mex_duration_t>().count()));

  // populate video properties with the first video stream info (if available)
  auto spec =
      std::find_if(streams.begin(), streams.end(), [this](const auto &spec) {
        return dynamic_cast<ffmpeg::IMediaHandler &>(reader.getStream(spec))
                   .getMediaType() == AVMEDIA_TYPE_VIDEO;
      });
  if (spec != streams.end())
  {
    auto p = dynamic_cast<const ffmpeg::VideoParams &>(
        reader.getStream(*spec).getMediaParams());

    mxSetProperty(mxObj, 0, "Height", mxCreateDoubleScalar(p.height));
    mxSetProperty(mxObj, 0, "Width", mxCreateDoubleScalar(p.width));
    mxSetProperty(mxObj, 0, "FrameRate",
                  mxCreateDoubleScalar(av_q2d(p.frame_rate)));
    mxSetProperty(mxObj, 0, "PixelAspectRatio",
                  mxCreateDoubleScalar(av_q2d(p.sample_aspect_ratio)));
  }

  // populate audio properties with the first audio stream info (if available)
  spec = std::find_if(streams.begin(), streams.end(), [this](const auto &spec) {
    return dynamic_cast<ffmpeg::IMediaHandler &>(reader.getStream(spec))
               .getMediaType() == AVMEDIA_TYPE_AUDIO;
  });
  if (spec != streams.end())
  {
    ffmpeg::IAudioHandler &ahdl =
        dynamic_cast<ffmpeg::IAudioHandler &>(reader.getStream(*spec));

    mxSetProperty(mxObj, 0, "NumberOfAudioChannels",
                  mxCreateDoubleScalar(ahdl.getChannels()));
    mxSetProperty(mxObj, 0, "SampleRate",
                  mxCreateDoubleScalar(ahdl.getSampleRate()));
    mxSetProperty(mxObj, 0, "ChannelLayout",
                  mxCreateString(ahdl.getChannelLayoutName().c_str()));
  }

  // populate metadata
  mxSetProperty(mxObj, 0, "Metadata", mxCreateTags(reader.getMetadata()));
}

void mexFFmpegReader::set_streams(const mxArray *mxObj)
{
  std::string spec;
  mxArray *mxStreams = mxGetProperty(mxObj, 0, "Streams");
  // Matlab object's Streams property is pre-formatted to be either:
  // * Empty      - auto select
  // * Cell array - user specified, consisting of specifier strings or a vector
  // of stream id's

  if (mxIsEmpty(mxStreams)) // auto-select streams
  {
    // auto-selection rules:
    // * No filtergraph: pick one video and one audio, video first
    // * Filtergraph: pick all the filter outputs
    if (filt_desc.empty())
    {
      auto add = [this, mxObj](const AVMediaType type,
                               const std::string &prefix) {
        // add the best stream (throws InvalidStreamSpecifier if invalid)
        int id = add_stream(mxObj, type);

        // use the "prefix + #" format specifier for the ease of type id
        bool notfound = true;
        if (reader.getStreamId(prefix) == id)
        {
          streams.push_back(prefix);
          notfound = false;
        }
        for (int i = 0; notfound && i < reader.getStreamCount(); ++i)
        {
          std::string spec = prefix + ":" + std::to_string(i);
          if (reader.getStreamId(spec) == id)
          {
            streams.push_back(spec);
            notfound = false;
          }
        }
      };

      try // to get the best video stream
      {
        add(AVMEDIA_TYPE_VIDEO, "v");
      }
      catch (ffmpeg::InvalidStreamSpecifier &)
      { // ignore
      }

      try // to get the best audio stream
      {
        add(AVMEDIA_TYPE_AUDIO, "a");
      }
      catch (ffmpeg::InvalidStreamSpecifier &)
      { // ignore
      }

      if (streams.empty())
        mexErrMsgIdAndTxt("ffmpeg:Reader:InvalidFile",
                          "Specified media file does not have either video or "
                          "audio streams.");
    }
    else
    {
      // add all the filtered streams
      while ((spec = reader.getNextInactiveStream(
                  "", AVMEDIA_TYPE_UNKNOWN, ffmpeg::StreamSource::FilterSink))
                 .size())
      {
        add_stream(mxObj, spec);
        streams.push_back(spec);
      }
    }
  }
  else // user specified streams
  {
    for (int i = 0; i < mxGetNumberOfElements(mxStreams); ++i)
    {
      mxArray *mxStream = mxGetCell(mxStreams, i);
      if (mxIsChar(mxStream))
      {
        try // to get the best audio stream
        {
          add_stream(mxObj, mexGetString(mxStream));
        }
        catch (ffmpeg::InvalidStreamSpecifier &)
        {
          mexErrMsgIdAndTxt("ffmpeg:Reader:InvalidStream",
                            "Specified stream specifier (\"%s\") does not "
                            "yield a stream or "
                            "the specified stream has already been selected.",
                            mexGetString(mxStream).c_str());
        }
        streams.push_back(spec);
      }
      else // if mxIsDouble(mxStream))
      {
        double *ids = mxGetPr(mxStream);
        for (int j = 0; j < mxGetNumberOfElements(mxStream); ++j)
        {
          int id = (int)ids[j];
          try
          {
            add_stream(mxObj, id);
          }
          catch (ffmpeg::InvalidStreamSpecifier &)
          {
            mexErrMsgIdAndTxt(
                "ffmpeg:Reader:InvalidStream",
                "Specified stream id (\"%d\") does not yield a stream or the "
                "specified stream has already been selected.",
                id);
          }
          streams.push_back(std::to_string(id));
        }
      }
    }
  }
}

void mexFFmpegReader::set_postops(mxArray *mxObj)
{
  AVPixelFormat pixfmt = AV_PIX_FMT_NB;
  AVSampleFormat samplefmt = AV_SAMPLE_FMT_NB;

  // video format: AV_PIX_FMT_NB->pick RGB/Grayscale| AV_PIX_FMT_NONE->native
  mxArray *mxFormat = mxGetProperty(mxObj, 0, "VideoFormat");
  if (!mxIsEmpty(mxFormat))
  {
    std::string pixdesc = mexGetString(mxFormat);
    if (pixdesc == "native")
      pixfmt = AV_PIX_FMT_NONE;
    else if (pixdesc == "Grayscale")
      pixfmt = AV_PIX_FMT_GRAY8;
    else
      pixfmt = av_get_pix_fmt(pixdesc.c_str());
  }

  // audio format: AV_SAMPLE_FMT_NB->native
  mxFormat = mxGetProperty(mxObj, 0, "AudioFormat");
  if (!mxIsEmpty(mxFormat))
  {
    std::string sampledesc = mexGetString(mxFormat);
    if (sampledesc != "native")
      samplefmt = av_get_sample_fmt(sampledesc.c_str());
  }

  for (auto &spec : streams)
  {
    auto &st = dynamic_cast<ffmpeg::IMediaHandler &>(reader.getStream(spec));
    auto type = st.getMediaType();
    if (type == AVMEDIA_TYPE_VIDEO)
    {
      // set post-filter to transpose & change video format
      if (pixfmt == AV_PIX_FMT_NONE || pixfmt == AV_PIX_FMT_NB)
      {
        auto nativefmt = dynamic_cast<ffmpeg::IVideoHandler &>(st).getFormat();
        if (pixfmt == AV_PIX_FMT_NB) // default
        {
          if (av_pix_fmt_desc_get(nativefmt)->nb_components == 1)
            pixfmt = AV_PIX_FMT_GRAY8;
          else
            pixfmt = AV_PIX_FMT_RGB24;

          // update
          mxSetProperty(mxObj, 0, "VideoFormat",
                        mxCreateString(av_get_pix_fmt_name(pixfmt)));
        }
        else // native
        {
          mxSetProperty(mxObj, 0, "VideoFormat",
                        mxCreateString(av_get_pix_fmt_name(nativefmt)));
          reader.setPostOp<mexFFmpegVideoPostOp, const AVPixelFormat>(
              spec, nativefmt);
        }
      }
      if (pixfmt != AV_PIX_FMT_NONE)
        reader.setPostOp<mexFFmpegVideoPostOp, const AVPixelFormat>(spec,
                                                                    pixfmt);
    }
    else if (type == AVMEDIA_TYPE_AUDIO)
    {
      auto nativefmt = dynamic_cast<ffmpeg::IAudioHandler &>(st).getFormat();
      if (samplefmt == AV_SAMPLE_FMT_NB) // auto/native
      {
        mxSetProperty(mxObj, 0, "AudioFormat",
                      mxCreateString(av_get_sample_fmt_name(
                          av_get_packed_sample_fmt(nativefmt))));
        samplefmt = nativefmt;
      }
      // pick planer/packed data format depending on whether to combine frames
      samplefmt = av_get_packed_sample_fmt(samplefmt);

      // if requested format is different from the stream format, set postop
      if (samplefmt != nativefmt)
        reader.setPostOp<mexFFmpegAudioPostOp, const AVSampleFormat>(spec,
                                                                     samplefmt);
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////
// Static private member functions

mxArray *mexFFmpegReader::mxCreateFileFormatName(AVPixelFormat fmt)
{
  if (fmt == AV_PIX_FMT_RGB24)
    return mxCreateString("RGB24");
  else if (fmt == AV_PIX_FMT_GRAY8)
    return mxCreateString("Grayscale");
  else
    return mxCreateString(av_get_pix_fmt_name(fmt));
}

mxArray *mexFFmpegReader::getFileFormats() // formats = getFileFormats();
{
  // return getMediaOutputFormats([](AVOutputFormat *fmt) {
  //   return (fmt->video_codec != AV_CODEC_ID_NONE &&
  //           (fmt->flags & AVFMT_NOTIMESTAMPS));
  // });
  return nullptr;
}

mxArray *mexFFmpegReader::getVideoFormats() // formats = getVideoFormats();
{
  return nullptr;
  //     // build a list of pixel format descriptors
  //     std::vector<const AVPixFmtDescriptor *>
  //         pix_descs;
  // pix_descs.reserve(256);
  // for (const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_next(NULL);
  //      pix_desc != NULL; pix_desc = av_pix_fmt_desc_next(pix_desc))
  // {
  //   AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(pix_desc);
  //   if (sws_isSupportedOutput(pix_fmt) &&
  //       mexComponentBuffer::supportedPixelFormat(pix_fmt))
  //     pix_descs.push_back(pix_desc);
  // }

  // std::sort(
  //     pix_descs.begin(), pix_descs.end(),
  //     [](const AVPixFmtDescriptor *a, const AVPixFmtDescriptor *b) -> bool
  //     {
  //       return strcmp(a->name, b->name) < 0;
  //     });

  // const int nfields = 11;
  // const char *fieldnames[11] = {
  //     "Name",  "Alias",       "NumberOfComponents", "BitsPerPixel",
  //     "RGB",   "Alpha",       "Paletted",           "HWAccel",
  //     "Bayer", "Log2ChromaW", "Log2ChromaH"};

  // plhs[0] = mxCreateStructMatrix(pix_descs.size(), 1, nfields, fieldnames);

  // for (int j = 0; j < pix_descs.size(); ++j)
  // {
  //   const AVPixFmtDescriptor *pix_fmt_desc = pix_descs[j];
  //   AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(pix_fmt_desc);
  //   mxSetField(plhs[0], j, "Name", mxCreateString(pix_fmt_desc->name));
  //   mxSetField(plhs[0], j, "Alias", mxCreateString(pix_fmt_desc->alias));
  //   mxSetField(plhs[0], j, "NumberOfComponents",
  //              mxCreateDoubleScalar(pix_fmt_desc->nb_components));
  //   mxSetField(plhs[0], j, "Log2ChromaW",
  //              mxCreateDoubleScalar(pix_fmt_desc->log2_chroma_w));
  //   mxSetField(plhs[0], j, "Log2ChromaH",
  //              mxCreateDoubleScalar(pix_fmt_desc->log2_chroma_h));
  //   mxSetField(plhs[0], j, "BitsPerPixel",
  //              mxCreateDoubleScalar(av_get_bits_per_pixel(pix_fmt_desc)));
  //   mxSetField(
  //       plhs[0], j, "Paletted",
  //       mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_PAL)
  //                          ? "on"
  //                          : (pix_fmt_desc->flags &
  //                          AV_PIX_FMT_FLAG_PSEUDOPAL)
  //                                ? "pseudo"
  //                                : "off"));
  //   mxSetField(plhs[0], j, "HWAccel",
  //              mxCreateString((pix_fmt_desc->flags &
  //              AV_PIX_FMT_FLAG_HWACCEL)
  //                                 ? "on"
  //                                 : "off"));
  //   mxSetField(plhs[0], j, "RGB",
  //              mxCreateString(
  //                  (pix_fmt_desc->flags & AV_PIX_FMT_FLAG_RGB) ? "on" :
  //                  "off"));
  //   mxSetField(plhs[0], j, "Alpha",
  //              mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_ALPHA)
  //                                 ? "on"
  //                                 : "off"));
  //   mxSetField(plhs[0], j, "Bayer",
  //              mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_BAYER)
  //                                 ? "on"
  //                                 : "off"));
  // }
}

// mxArray *
// mexFFmpegReader::getVideoCompressions() // formats =
// getVideoCompressions();
// {
//   return ::getMediaCompressions([](const AVCodecDescriptor *desc) -> bool)
//   {
//     return avcodec_find_decoder(desc->id) && desc->type ==
//     AVMEDIA_TYPE_VIDEO
//     &&
//            !strstr(desc->name, "_deprecated");
//   });
// }

// mxArray *mexFFmpegReader::getVideoFormats()
// {
//   return getVideoFormats([](const AVPixelFormat pix_fmt) -> bool {
//     // supported by the IO buffers (8-bit, no subsampled components)
//     if (!ffmpeg::imageCheckComponentSize(pix_fmt)) return false;

//     // supported by SWS library
//     return sws_isSupportedInput(pix_fmt) && sws_isSupportedOutput(pix_fmt);
//   });
// }

// // tf = isSupportedFormat(format_name);
// mxArray *mexFFmpegReader::isSupportedFormat(const mxArray *prhs)
// {
//   return ::isSupportedVideoFormat(
//       prhs, [](const AVPixelFormat pix_fmt) -> bool {
//         // must <= 8-bit/component
//         if (!ffmpeg::imageCheckComponentSize(pix_fmt)) return false;

//         // supported by SWS library
//         return sws_isSupportedInput(pix_fmt) &&
//         sws_isSupportedOutput(pix_fmt);
//       });
// }

// AVPixelFormat mexFFmpegReader::mexArrayToFormat(const mxArray *obj)
// {
//   return ::mexArrayToFormat(obj, [](const AVPixelFormat pix_fmt) -> bool {
//     return mexImageFilter::isSupportedFormat(pix_fmt);
//   });
// }

// // validateSARString(SAR_expression);
// void mexFFmpegReader::validateSARString(const mxArray *prhs)
// {
//   AVRational sar = mexParseRatio(prhs);
//   if (sar.num <= 0 || sar.den <= 0)
//     mexErrMsgTxt("SAR expression must result in a positive rational
//     number.");
// }

// AVRational mexFFmpegReader::mexArrayToSAR(const mxArray *mxSAR)
// {
//   if (mxIsChar(mxSAR)) { return mexParseRatio(mxSAR); }
//   else if (mxIsScalar(mxSAR))
//   {
//     return av_d2q(mxGetScalar(mxSAR), INT_MAX);
//   }
//   else // 2-elem
//   {
//     double *data = mxGetPr(mxSAR);
//     return av_make_q((int)data[0], (int)data[1]);
//   }
// }
