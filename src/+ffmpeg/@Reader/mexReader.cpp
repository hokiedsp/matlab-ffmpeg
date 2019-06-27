#include "mexReader.h"

#include "../../ffmpeg/ffmpegImageUtils.h"
#include "../../ffmpeg/ffmpegPtrs.h"

#include <mexGetString.h>

// #include "getMediaCompressions.h"
// #include "getVideoFormats.h"

// #include <algorithm>

extern "C"
{
#include <libavutil/samplefmt.h>
  // #include <libavutil/frame.h> // for AVFrame
  // #include <libavutil/pixfmt.h>
  // #include <libavutil/pixdesc.h>
  // #include <libswscale/swscale.h>
}

// #include <cstdarg>
// #include <locale>
// #include <experimental/filesystem> // C++-standard header file name
#include <filesystem>

namespace fs = std::filesystem;

// // append transpose filter at the end to show the output in the proper
// orientation in MATLAB

// #include <fstream>
// std::ofstream output("mextest.csv");
void mexFFmpegCallback(void *avcl, int level, const char *fmt, va_list argptr)
{
  if (level <= AV_LOG_INFO) // AV_LOG_FATAL || level == AV_LOG_ERROR)
  {
    char dest[1024 * 16];
    vsprintf(dest, fmt, argptr);
    mexPrintf(dest);
  }
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{

  av_log_set_callback(&mexFFmpegCallback);

  mexObjectHandler<mexFFmpegReader>(nlhs, plhs, nrhs, prhs);
}

//////////////////////////////////////////////////////////////////////////////////

// mexFFmpegReader(mobj) (all arguments  pre-validated)
mexFFmpegReader::mexFFmpegReader(const mxArray *mxObj, int nrhs,
                                 const mxArray *prhs[])
{
  // get absolute path to the file
  fs::path p(mexGetString(mxGetProperty(mxObj, 0, "Path")));

  // open the video file
  reader.openFile(p.string());

  // reserve one temp frame
  add_frame();
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
    double t = mxGetScalar(prhs[0]);
    setCurrentTime(t);
  }
  else if (command == "getCurrentTime")
  {
    double t(NAN);
    plhs[0] = mxCreateDoubleScalar(t);
  }
  else if (command == "addStreams")
    addStreams(mxObj);
  else if (command == "activate")
    activate(mxObj);
  else if (command == "readFrame")
    readFrame(nlhs, plhs, nrhs, prhs);
  else if (command == "read")
    read(nlhs, plhs, nrhs, prhs);
  else if (command == "hasFrame")
    plhs[0] = mxCreateLogicalScalar(hasFrame());
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
    if (nrhs != 1 || !mxIsChar(prhs[0]))
      throw std::runtime_error(
          "validate_pixfmt0() takes one string input argument.");

    std::string pixfmt = mexGetString(prhs[0]);
    if (av_get_pix_fmt(pixfmt.c_str()) == AV_PIX_FMT_NONE)
      mexErrMsgIdAndTxt("ffmpeg:VideoReader:validate_pixfmt:invalidFormat",
                        "%s is not a valid FFmpeg Pixel Format",
                        pixfmt.c_str());
  }
  else
    return false;
  return true;
}

////////////////////////////////////////////////////////////////////////////////////////

void mexFFmpegReader::setCurrentTime(double t, const bool reset_buffer) {}

bool mexFFmpegReader::hasFrame() { return !reader.EndOfStream(streams[0]); }

//[frame1,frame2,...] = readFrame(obj, varargin);
void mexFFmpegReader::readFrame(int nlhs, mxArray *plhs[], int nrhs,
                                const mxArray *prhs[])
{
  if (!hasFrame())
    mexErrMsgIdAndTxt("ffmpeg:Reader:EndOfFile",
                      "No more frames available to read from file.");

  AVFrame *frame = av_frame_alloc();
  ffmpeg::AVFramePtr frame_cleanup(frame, &ffmpeg::delete_av_frame);

  // read the first stream
  ts = read_frame(plhs[0]);

  // for each stream outputs
  for (int i = 1; i < streams.size(); ++i)
    read_frame((i < nlhs) ? plhs[i] : nullptr, streams[i]);

  // std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  // if (!rd_buf->available()) buffer_ready.wait(buffer_guard);
  // rd_buf->read_frame(dst, (nlhs > 1) ? &t : NULL);
  // buffer_ready.notify_one();
  // buffer_guard.unlock();

  // if (nlhs > 1) plhs[1] = mxCreateDoubleScalar(t);
}

double mexFFmpegReader::read_frame(mxArray *mxData)
{
  const std::string &spec = streams[0];

  auto frame = frames[0];

  // read next frame for the primary stream
  reader.readNextFrame(frame, spec, true);

  // automatically unreference frame when getting out of this function
  std::unique_ptr<AVFrame, decltype(&av_frame_unref)> auto_unref_frame(
      frame, &av_frame_unref);

  ffmpeg::IAVFrameSource &src = reader.getStream(spec);
  if (src.getMediaType() == AVMEDIA_TYPE_VIDEO)
  {
    ffmpeg::IVideoHandler &vsrc = dynamic_cast<ffmpeg::IVideoHandler &>(src);
    int width = frame->width;
    int height = frame->height;
    AVPixelFormat format = (AVPixelFormat)frame->format;

    int frame_data_sz =
        ffmpeg::imageGetComponentBufferSize(format, width, height);

    mwSize dims[3] = {(mwSize)width, (mwSize)height,
                      (mwSize)frame_data_sz / (width * height)};
    mxData = mxCreateNumericArray(3, dims, mxUINT8_CLASS, mxREAL);
    uint8_t *data = (uint8_t *)mxGetData(mxData);

    ffmpeg::imageCopyToComponentBuffer(data, frame_data_sz, frame->data,
                                       frame->linesize, format, frame->width,
                                       frame->height);
  }
  else if (src.getMediaType() == AVMEDIA_TYPE_AUDIO)
  {
    ffmpeg::IAudioHandler &asrc = dynamic_cast<ffmpeg::IAudioHandler &>(src);
    AVSampleFormat fmt =
        av_get_packed_sample_fmt((AVSampleFormat)frame->format);
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

    bool is_planar = (fmt != (AVSampleFormat)frame->format);
    mwSize dims[2] = {(mwSize)frame->nb_samples, (mwSize)frame->channels};
    if (is_planar) std::swap(dims[0], dims[1]);
    mxData = mxCreateNumericArray(2, dims, mx_class, mxREAL);
    uint8_t *data = (uint8_t *)mxGetData(mxData);

    int linesize;
    av_samples_get_buffer_size(&linesize, frame->channels, frame->nb_samples,
                               fmt, false);
    uint8_t *dst[AV_NUM_DATA_POINTERS];
    for (int i = 0; i < frame->channels; ++i) dst[i] = data + i * linesize;

    av_samples_copy(dst, frame->data, 0, 0, frame->nb_samples, frame->channels,
                    fmt);
  }
  else
  {
    throw ffmpeg::Exception("Encountered data from an unexpected stream.");
  }

  return reader.getNextTimeStamp(spec);
}
void mexFFmpegReader::read_frame(mxArray *mxData, const std::string &spec)
{
  // int nframes = 0;
  // auto unref_frames = [&nframes](std::vector<AVFrame *> *frames) {
  //   for (int i = 0; i < nframes; ++i) av_frame_unref((*frames)[i]);
  // };
  // std::unique_ptr<std::vector<AVFrame *>, decltype(&unref_frames)> auto_unref(
  //     &frames, &unref_frames);

  // while (reader.getNextTimeStamp(spec) < ts)
  // {
  //   if (frames.size() < nframes) add_frame();
  //   reader.readNextFrame(frames[nframes], spec, true);
  //   ++nframes;
  // }
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

void mexFFmpegReader::addStreams(const mxArray *mxObj)
{
  // validation lambda for VideoFilter & AudioFilter options
  auto validate_fg = [](std::string desc, AVMediaType type) {
    ffmpeg::filter::Graph fg(desc);
    return std::any_of(
        fg.beginOutputFilter(), fg.endOutputFilter(),
        [&](auto out) { return out.second->getMediaType() != type; });
  };

  // validate video filter graph
  std::string vdesc(mexGetString(mxGetProperty(mxObj, 0, "VideoFilter")));
  if (vdesc.size())
  {
    if (validate_fg(vdesc, AVMEDIA_TYPE_VIDEO))
      throw ffmpeg::Exception("VideoFilter must only output video streams.");

    // filter validated,
    filt_desc = vdesc;
  }

  std::string adesc(mexGetString(mxGetProperty(mxObj, 0, "AudioFilter")));
  if (adesc.size())
  {
    if (validate_fg(adesc, AVMEDIA_TYPE_AUDIO))
      throw ffmpeg::Exception("AudioFilter must only output audio streams.");
    if (vdesc.size()) filt_desc += ';';
    filt_desc += adesc;
  }

  // all clear, add the filter graph to the reader
  if (filt_desc.size()) reader.setFilterGraph(filt_desc);

  ///////

  mxArray *mxStreams = mxGetProperty(mxObj, 0, "Streams");
  bool auto_select[2] = {
      false, false}; // true if streams are to be selected automatically
  auto get_streams = // lambda to validate & get stream specs
      [&](const mxArray *mxStream) {
        if (mxIsChar(mxStream))
        {
          std::string spec = mexGetString(mxStream);
          if (reader.getStreamId(spec) == AVERROR_STREAM_NOT_FOUND) throw;
          streams.push_back(spec);
        }
        else // pre-validated to be double array
        {
          double *d_list = mxGetPr(mxStream);
          for (int i = 0; i < mxGetNumberOfElements(mxStream); ++i)
          {
            int id = (int)d_list[i];
            if (reader.getStreamId(id) == AVERROR_STREAM_NOT_FOUND) throw;
            streams.push_back(std::to_string(id));
          }
        }
      };
  try
  {
    if (mxIsCell(mxStreams))
    {
      for (int i = 0; i < mxGetNumberOfElements(mxStreams); ++i)
        get_streams(mxGetCell(mxStreams, i));
    }
    else
    {
      if (!mxIsChar(mxStreams))
        get_streams(mxStreams);
      else
      {
        auto label = mexGetString(mxStreams);
        bool both = (label == "auto");
        if (both || label == "noaudio")
          auto_select[0] = true;
        else if (both || label == "novideo")
          auto_select[1] = true;
        else
          get_streams(mxStreams);
      }
    }
  }
  catch (...)
  {
    throw ffmpeg::Exception(
        "Specified Streams property contains invalid stream specifier.");
  }

  // auto-configure video streams if so specified
  if (auto_select[0])
  {
    if (vdesc.size()) // get all video filter output
    {
      std::string spec;
      while ((spec = reader.getNextInactiveStream(spec, AVMEDIA_TYPE_VIDEO, -1))
                 .size())
      {
        reader.addStream(spec);
        streams.push_back(spec);
      }
    }
    else // get the best stream
    {
      streams.push_back(std::to_string(reader.addStream(AVMEDIA_TYPE_VIDEO)));
    }
  }

  // auto-configure audio streams if so specified
  if (auto_select[1])
  {
    if (adesc.size()) // get all audio filter output
    {
      std::string spec;
      while ((spec = reader.getNextInactiveStream(spec, AVMEDIA_TYPE_AUDIO, -1))
                 .size())
      {
        reader.addStream(spec);
        streams.push_back(spec);
      }
    }
    else // get the best stream
    {
      streams.push_back(std::to_string(reader.addStream(AVMEDIA_TYPE_AUDIO)));
    }
  }
}

void mexFFmpegReader::activate(const mxArray *mxObj)
{
  // activate the reader
  reader.activate();

  // fill until all streams have at least one frame in the buffer

  // fill the buffers

  // set Matlab class properties
  // mxSetProperty((mxArray *)prhs[0], 0, "Name",
  //               mxCreateString(p.filename().string().c_str()));
  // mxSetProperty((mxArray *)prhs[0], 0, "Path",
  //               mxCreateString(p.parent_path().string().c_str()));
  // mxSetProperty((mxArray *)prhs[0], 0, "FrameRate",
  //               mxCreateDoubleScalar((double)reader.getFrameRate()));
  // mxSetProperty((mxArray *)prhs[0], 0, "Width",
  //               mxCreateDoubleScalar((double)reader.getHeight()));
  // mxSetProperty((mxArray *)prhs[0], 0, "Height",
  //               mxCreateDoubleScalar((double)reader.getWidth()));
  // mxArray *sar = mxCreateDoubleMatrix(1, 2, mxREAL);
  // *mxGetPr(sar) = (double)reader.getSAR().den;
  // *(mxGetPr(sar) + 1) = (double)reader.getSAR().num;
  // mxSetProperty((mxArray *)prhs[0], 0, "PixelAspectRatio", sar);
}

/////////////////////////////////////////////////////////////////////////////////
// Static private member functions

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
  //     [](const AVPixFmtDescriptor *a, const AVPixFmtDescriptor *b) -> bool {
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
  //              mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_HWACCEL)
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
// mexFFmpegReader::getVideoCompressions() // formats = getVideoCompressions();
// {
//   return ::getMediaCompressions([](const AVCodecDescriptor *desc) -> bool)
//   {
//     return avcodec_find_decoder(desc->id) && desc->type == AVMEDIA_TYPE_VIDEO
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
