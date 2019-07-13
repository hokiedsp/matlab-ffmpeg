#include <mex.h>

#include "../../ffmpeg/ffmpegReader.h"

#include "../../ffmpeg/mxutils.h"

extern "C"
{
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>

// %   INFO = ffmpeg.AUDIOINFO(FILENAME) returns a structure whose fields
// %   contain information about an audio file. FILENAME is a character vector
// %   or string scalar that specifies the name of the audio file. FILENAME
// %   must be in the current directory, in a directory on the MATLAB path, or
// %   a full path to a file.
// %
// %   The set of fields in INFO depends on the individual file and its
// %   format.  However, the first nine fields are always the same. These
// %   common fields are:
// %
// %   'Filename'          A character vector or string scalar containing the
// %                       name of the file
// %   'CompressionMethod' Method of audio compression in the file
// %   'NumChannels'       Number of audio channels in the file.
// %   'SampleRate'        The sample rate (in Hertz) of the data in the file.
// %   'TotalSamples'      Total number of audio samples in the file.
// %   'Duration'          Total duration of the audio in the file, in
// %                       seconds.
// %   'Title'             character vector or string scalar representing the
// %                       value of the Title tag present in the file. Value
// %                       is empty if tag is not present.
// %   'Comment'           character vector or string scalar representing the
// %                       value of the Comment tag present in the file. Value
// %                       is empty if tag is not present.
// %   'Artist'            character vector or string scalar representing the
// %                       value of the Artist or Author tag present in the
// %                       file. Value is empty if tag not present.
// %
// %   Format specific fields areas follows:
// %
// %   'BitsPerSample'     Number of bits per sample in the audio file.
// %                       Only supported for WAVE (.wav) and FLAC (.flac)
// %                       files. Valid values are 8, 16, 24, 32 or 64.
// %
// %   'BitRate'           Number of kilobits per second (kbps) used for
// %                       compressed audio files. In general, the larger the
// %                       BitRate, the higher the compressed audio quality.
// %                       Only supported for MP3 (.mp3) and MPEG-4 Audio
// %                       (.m4a, .mp4) files.

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  // initialize ffmpeg::Exception
  ffmpeg::Exception::initialize();
  ffmpeg::Exception::log_fcn = [](const auto &msg) { mexPrintf(msg.c_str()); };

  if (nlhs > 1 || nrhs != 1) mexErrIdText();

  ffmpeg::Reader reader;

  // open the video file
  reader.openFile(mexGetString(prhs[0]));

  // add audio stream
  int stream_id = reader.addStream(AVMEDIA_TYPE_AUDIO);

  const char[][] = {"Filename",
                    "Stream",
                    "CompressionMethod",
                    "NumChannels"
                    "SampleRate",
                    "TotalSamples",
                    "Duration",
                    "Title",
                    "Comment"
                    "Artist"};

  // set Matlab class properties
  mxArray *mxData;
  mxData = mxCreateCellMatrix(1, streams.size());
  for (int i = 0; i < streams.size(); ++i)
    mxSetCell(mxData, i, mxCreateString(streams[i].c_str()));
  mxSetProperty(mxObj, 0, "Streams", mxData);

  mxSetProperty(
      mxObj, 0, "Duration",
      mxCreateDoubleScalar(reader.getDuration<mex_duration_t>().count()));

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
  // * Cell array - user specified, consisting of specifier strings or a
  // vector of stream id's

  if (mxIsEmpty(mxStreams)) // auto-select streams
  {
    // auto-selection rules:
    // * No filtergraph: pick one video and one audio, video first
    // * Filtergraph: pick all the filter outputs
    if (filt_desc.empty())
    {
      auto add = [this](const AVMediaType type, const std::string &prefix) {
        // add the best stream (throws InvalidStreamSpecifier if invalid)
        int id = reader.addStream(type);

        // use the "prefix + #" format specifier for the ease of type id
        bool notfound = true;
        for (int i = 0; notfound && i < reader.getStreamCount(); ++i)
        {
          std::string spec = prefix + std::to_string(i);
          if (reader.getStreamId(spec) == id)
          {
            streams.push_back(spec);
            notfound = false;
          }
        }
      };

      try // to get the best video stream
      {
        add(AVMEDIA_TYPE_VIDEO, "v:");
      }
      catch (ffmpeg::InvalidStreamSpecifier &)
      { // ignore
      }

      try // to get the best audio stream
      {
        add(AVMEDIA_TYPE_AUDIO, "a:");
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
                  "", AVMEDIA_TYPE_UNKNOWN,
                  ffmpeg::Reader::StreamSource::FilterSink))
                 .size())
      {
        reader.addStream(spec);
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
          reader.addStream(mexGetString(mxStream));
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
            reader.addStream(id);
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
