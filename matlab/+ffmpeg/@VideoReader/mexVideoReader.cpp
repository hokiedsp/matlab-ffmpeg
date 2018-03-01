#include "mexVideoReader.h"

#include "getMediaCompressions.h"
#include "getVideoFormats.h"

#include "ffmpegImgUtils.h"

#include <algorithm>

extern "C" {
#include <libavutil/frame.h> // for AVFrame
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <cstdarg>
#include <locale>
// #include <experimental/filesystem> // C++-standard header file name
#include <filesystem>
using namespace std::experimental::filesystem::v1;
namespace fs = std::experimental::filesystem;

// // append transpose filter at the end to show the output in the proper orientation in MATLAB

// #include <fstream>
// std::ofstream output("mextest.csv");
void mexFFmpegCallback(void *avcl, int level, const char *fmt, va_list argptr)
{
  if (level <= AV_LOG_INFO) //AV_LOG_FATAL || level == AV_LOG_ERROR)
  {
    char dest[1024 * 16];
    vsprintf(dest, fmt, argptr);
    mexPrintf(dest);
  }
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{

  av_log_set_callback(&mexFFmpegCallback);

  mexObjectHandler<mexVideoReader>(nlhs, plhs, nrhs, prhs);
}

//////////////////////////////////////////////////////////////////////////////////

// mexVideoReader(mobj, filename) (all arguments  pre-validated)
mexVideoReader::mexVideoReader(const mxArray *mxObj, int nrhs, const mxArray *prhs[])
    : rd_rev(false), state(OFF), killnow(false)
{
  // open the file
  open_file(mxObj, mexGetString(prhs[1]));

  // configure the buffers
  buffers.reserve(2);
  buffers.emplace_back(buffer_capacity, reader.getWidth(), reader.getHeight(), reader.getPixelFormat(), !rd_rev);
  buffers.emplace_back(buffer_capacity, reader.getWidth(), reader.getHeight(), reader.getPixelFormat(), !rd_rev);
  wr_buf = buffers.begin();
  rd_buf = wr_buf + 1;
  reader.resetBuffer(&*wr_buf);

  // start the reader thread
  frame_writer = std::thread(&mexVideoReader::shuffle_buffers, this);
}

mexVideoReader::~mexVideoReader()
{
  // av_log(NULL, AV_LOG_INFO, "mexVideoReader::~mexVideoReader::destruction started\n");
  killnow = true;

  // stop shuffle_buffers thread if it is waiting for read buffer to be read
  {
    std::unique_lock<std::mutex> buffer_guard(buffer_lock);
    buffer_ready.notify_one();
  }

  // close the file before buffers are destroyed
  reader.closeFile();
  // av_log(NULL, AV_LOG_INFO, "mexVideoReader::~mexVideoReader::file closed\n");

  // start the file reading thread (sets up and idles)
  if (frame_writer.joinable())
    frame_writer.join();

  // av_log(NULL, AV_LOG_INFO, "mexVideoReader::~mexVideoReader::destruction completed\n");
}

bool mexVideoReader::action_handler(const mxArray *mxObj, const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  if (command == "setCurrentTime")
  {
    if (!(mxIsNumeric(prhs[0]) && mxIsScalar(prhs[0])) || mxIsComplex(prhs[0]))
      throw 0;

    // get new time
    double t = mxGetScalar(prhs[0]);

    setCurrentTime(t);
  }
  else if (command == "getDuration") // integer between -10 and 10
  {
    plhs[0] = mxCreateDoubleScalar(reader.getDuration());
  }
  else if (command == "getBitsPerPixel") // integer between -10 and 10
  {
    plhs[0] = mxCreateDoubleScalar(reader.getBitsPerPixel());
  }
  else if (command == "getVideoCompression")
  {
    std::string name = reader.getCodecName();
    std::string desc = reader.getCodecDescription();
    if (desc.size())
      name += " (" + desc + ')';
    plhs[0] = mxCreateString(name.c_str());
  }
  else if (command == "getCurrentTime")
  {
    double t(NAN);

    std::unique_lock<std::mutex> buffer_guard(buffer_lock);
    if (rd_buf->eof())
      t = reader.getDuration();
    else
    {
      if (!rd_buf->available())
        buffer_ready.wait(buffer_guard);
      rd_buf->read_frame(NULL, &t, false);
      buffer_ready.notify_one();
    }
    plhs[0] = mxCreateDoubleScalar(t);
  }
  else if (command == "getAudioCompression") // integer between -10 and 10
  {
    plhs[0] = mxCreateString("");
  }
  else if (command == "getNumberOfAudioChannels")
  {
    plhs[0] = mxCreateDoubleMatrix(0, 0, mxREAL);
  }
  else if (command == "getNumberOfFrames")
  {
    plhs[0] = mxCreateDoubleScalar((double)reader.getNumberOfFrames());
  }
  else if (command == "readFrame")
    readFrame(nlhs, plhs, nrhs, prhs);
  else if (command == "readBuffer")
    readBuffer(nlhs, plhs, nrhs, prhs);
  else if (command == "read")
    read(nlhs, plhs, nrhs, prhs);
  else if (command == "hasFrame")
    plhs[0] = mxCreateLogicalScalar(hasFrame());
  return true;
}

bool mexVideoReader::static_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  if (command == "getFileFormats")
  {
    if (nrhs > 0)
      throw std::runtime_error("getFileFormats() takes no input argument.");
    mexVideoReader::getFileFormats(nlhs, plhs);
  }
  else if (command == "getVideoFormats")
  {
    if (nrhs > 0)
      throw std::runtime_error("getVideoFormats() takes no input argument.");
    mexVideoReader::getVideoFormats(nlhs, plhs);
  }
  else if (command == "getVideoCompressions")
  {
    if (nrhs > 0)
      throw std::runtime_error("getVideoCompressions() takes no input argument.");
    mexVideoReader::getVideoCompressions(nlhs, plhs);
    avcodec_find_decoder(desc->id) && desc->type == AVMEDIA_TYPE_VIDEO && !strstr(desc->name, "_deprecated")
  }
  else if (command == "validate_pixfmt")
  {
    if (nrhs != 1 || !mxIsChar(prhs[0]))
      throw std::runtime_error("validate_pixfmt0() takes one string input argument.");

    std::string pixfmt = mexGetString(prhs[0]);
    if (av_get_pix_fmt(pixfmt.c_str()) == AV_PIX_FMT_NONE)
      mexErrMsgIdAndTxt("ffmpeg:VideoReader:validate_pixfmt:invalidFormat", "%s is not a valid FFmpeg Pixel Format", pixfmt.c_str());
  }
  else
    return false;
  return true;
}

////////////////////////////////////////////////////////////////////////////////////////


void mexVideoReader::setCurrentTime(double t, const bool reset_buffer)
{
  std::unique_lock<std::mutex> buffer_guard(buffer_lock, std::defer_lock);
  // this should stop the shuffle_buffer thread when the buffer is not available

  if (reset_buffer)
  {
    buffer_guard.lock();
    reader.resetBuffer(NULL);
  }

  // if reading backwards, set to the time so the last frame in the buffer will be the requested time
  double T = reader.getDuration();
  if (rd_rev)
  {
    if (t <= 0.0)
    {
      state = OFF;
    }
    else
    {
      double Tbuf = buffer_capacity / reader.getFrameRate();
      if (t > T) // make sure the last frame read is the last frame
        t = T - Tbuf;
      else // buffer time span
        t -= Tbuf;
      state = ON;
    }
  }
  else if (t >= T)
    state = OFF;
  else
    state = ON;

  // av_log(NULL,AV_LOG_INFO,"setCurrentTime()::timestamp set to %f\n",t);

  // set new time
  reader.setCurrentTimeStamp(t);

  if (reset_buffer)
  {
    // reset buffers
    wr_buf->reset();
    rd_buf->reset();

    // set write buffer to reader
    reader.resetBuffer(&*wr_buf);

    // tell  shuffle_buffers thread to resume
    buffer_ready.notify_one();
  }
}

bool mexVideoReader::hasFrame()
{
  std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  return state != OFF || rd_buf->available();
}

void mexVideoReader::readFrame(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) //[frame,time] = readFrame(obj, varargin);
{
  // if buffer size is 1, use readBuffer() to avoid copying the frame
  if (buffer_capacity == 1)
  {
    readBuffer(nlhs, plhs, nrhs, prhs);
    return;
  }

  if (hasFrame())
  {
    mwSize dims[3] = {reader.getWidth(), reader.getHeight(), reader.getPixFmtDescriptor().nb_components};
    plhs[0] = mxCreateNumericArray(3, dims, mxUINT8_CLASS, mxREAL);
    uint8_t *dst = (uint8_t *)mxGetData(plhs[0]);
    double t(NAN);

    std::unique_lock<std::mutex> buffer_guard(buffer_lock);
    if (!rd_buf->available())
      buffer_ready.wait(buffer_guard);
    rd_buf->read_frame(dst, (nlhs > 1) ? &t : NULL);
    buffer_ready.notify_one();
    buffer_guard.unlock();

    if (nlhs > 1)
      plhs[1] = mxCreateDoubleScalar(t);
  }
  else
  {
    plhs[0] = mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL);
    if (nlhs > 1)
      plhs[1] = mxCreateDoubleMatrix(0, 0, mxREAL);
  }
}

void mexVideoReader::read(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) //varargout = read(obj, varargin);
{
  throw std::runtime_error("Not supported. Use readFrame() or readBuffer() instead.");
}

// [frames,timestamps] = readBuffer(obj)
void mexVideoReader::readBuffer(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  size_t nb_frames;
  uint8_t *data = NULL;
  double *ts = NULL;

  bool has_frame = hasFrame();
  if (has_frame) // Extract the data arrays from the buffer
  {
    // wait until a buffer is ready
    std::unique_lock<std::mutex> buffer_guard(buffer_lock);
    if (!rd_buf->full())
      buffer_ready.wait(buffer_guard);
    nb_frames = rd_buf->release(&data, &ts);

    // notify the stuffer for the buffer availability
    buffer_ready.notify_one();
  }

  // create output array
  mwSize dims[4] = {reader.getWidth(), reader.getHeight(), reader.getPixFmtDescriptor().nb_components, 0};
  plhs[0] = mxCreateNumericArray(4, dims, mxUINT8_CLASS, mxREAL);
  if (has_frame)
  {
    //if (state==OFF) // last buffer
    if (rd_rev && ts[0] == 0.0) // last buffer
    {
      // av_log(NULL, AV_LOG_INFO, "rd_rev_t_last = %f\n",rd_rev_t_last);
      auto tend = std::find_if(ts, ts + nb_frames, [&](const double &t) -> bool { return t >= rd_rev_t_last; });
      dims[3] = tend - ts;
    }
    else
      dims[3] = nb_frames;
    mxSetData(plhs[0], data);
  }
  mxSetDimensions(plhs[0], dims, 4);

  if (nlhs > 1) // create array for the time stamps if requested
  {
    plhs[1] = mxCreateDoubleMatrix(1, 0, mxREAL);
    if (has_frame)
    {
      mxSetN(plhs[1], dims[3]);
      mxSetPr(plhs[1], ts);
    }
  }
  else // if not, free up the memory
  {
    mxFree(ts);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////
// Private member functions

void mexVideoReader::open_file(const mxArray *mxObj, const std::string &filename)
{
  // get absolute path to the file
  fs::path p = fs::canonical(mexGetString(prhs[1]));

  // open the video file
  reader.openFile(p.string(), mex_get_filterdesc(prhs[0]), mexArrayToFormat(prhs[0]));

  // if read backwards, start from the end
  buffer_capacity = (int)mxGetScalar(mxGetProperty(prhs[0], 0, "BufferSize"));
  rd_rev = mexGetString(mxGetProperty(prhs[0], 0, "Direction")) == "backward";
  if (rd_rev)
    setCurrentTime(reader.getDuration(), false);
  else
    state = ON;

  // set unspecified properties
  mxSetProperty((mxArray *)prhs[0], 0, "Name", mxCreateString(p.filename().string().c_str()));
  mxSetProperty((mxArray *)prhs[0], 0, "Path", mxCreateString(p.parent_path().string().c_str()));
  mxSetProperty((mxArray *)prhs[0], 0, "FrameRate", mxCreateDoubleScalar((double)reader.getFrameRate()));
  mxSetProperty((mxArray *)prhs[0], 0, "Width", mxCreateDoubleScalar((double)reader.getHeight()));
  mxSetProperty((mxArray *)prhs[0], 0, "Height", mxCreateDoubleScalar((double)reader.getWidth()));
  mxArray *sar = mxCreateDoubleMatrix(1, 2, mxREAL);
  *mxGetPr(sar) = (double)reader.getSAR().den;
  *(mxGetPr(sar) + 1) = (double)reader.getSAR().num;
  mxSetProperty((mxArray *)prhs[0], 0, "PixelAspectRatio", sar);
}

void mexVideoReader::shuffle_buffers()
{
  std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  while (!killnow)
  {
    if (state == OFF || rd_buf->readyToRead()) // read buffer still has unread frames, wait until all read
    {
      // if (state==OFF)
      //   av_log(NULL,AV_LOG_INFO,"mexVideoReader::shuffle_buffers()::waiting till CurrentTime changed\n");
      // else
      //   av_log(NULL,AV_LOG_INFO,"mexVideoReader::shuffle_buffers()::waiting till rd_buf completely read\n");
      buffer_ready.wait(buffer_guard); // to be woken up by read functions
      // av_log(NULL,AV_LOG_INFO,"mexVideoReader::shuffle_buffers()::rd_buf read\n");
    }
    else // all read, wait till the other buffer is written then swap
    {
      // reader.atEndOfFile()
      // wait until write buffer is full
      // av_log(NULL,AV_LOG_INFO,"mexVideoReader::shuffle_buffers()::waiting till wr_buf filled\n");
      reader.blockTillBufferFull();
      // av_log(NULL,AV_LOG_INFO,"mexVideoReader::shuffle_buffers()::wr_buf filled (%d|%d)\n",wr_buf->size(),wr_buf->last());
      if (killnow)
        break;

      // done with the read buffer
      rd_buf->reset();

      // swap the buffers
      std::swap(wr_buf, rd_buf);

      if (rd_rev) // if reading in reverse direction
      {
        if (state == LAST)
        {
          state = OFF;
        }
        else
        { // set new timestamp
          double t;
          if (AVERROR_EOF == rd_buf->read_first_frame(NULL, &t))
            t = reader.getDuration();

          // av_log(NULL, AV_LOG_INFO, "mexVideoReader::shuffle_buffers()::setting time to %f\n", t);

          setCurrentTime(t, false);

          // override setCurrentTime's state
          if (state == OFF)
            state = LAST;
          else
            rd_rev_t_last = t;
        }
      }
      else if (rd_buf->last())
      { // if eof, stop till setCurrentTime() call
        // av_log(NULL,AV_LOG_INFO,"mexVideoReader::shuffle_buffers()::reached EOF\n");
        state = OFF;
      }

      // av_log(NULL, AV_LOG_INFO, "mexVideoReader::shuffle_buffers()::swapped, available frames in rd_buf:%d\n", rd_buf->full());
      // notify the waiting thread that rd_buf now contains a filled buffer
      buffer_ready.notify_one();

      // if more to read, give reader the new buffer
      if (state == ON)
        reader.resetBuffer(&*wr_buf);
    }
  }
  // av_log(NULL, AV_LOG_INFO, "mexVideoReader::shuffle_buffers()::exiting\n");
}

/////////////////////////////////////////////////////////////////////////////////
// Static private member functions

mxArray *mexVideoReader::getFileFormats() // formats = getFileFormats();
{
  return getMediaOutputFormats([](AVOutputFormat* fmt)
   {
      return (fmt->video_codec != AV_CODEC_ID_NONE && (fmt->flags&AVFMT_NOTIMESTAMPS));
       });
}

mxArray *mexVideoReader::getVideoFormats() // formats = getVideoFormats();
{
  return 
  // build a list of pixel format descriptors
  std::vector<const AVPixFmtDescriptor *> pix_descs;
  pix_descs.reserve(256);
  for (const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_next(NULL);
       pix_desc != NULL;
       pix_desc = av_pix_fmt_desc_next(pix_desc))
  {
    AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(pix_desc);
    if (sws_isSupportedOutput(pix_fmt) && mexComponentBuffer::supportedPixelFormat(pix_fmt))
      pix_descs.push_back(pix_desc);
  }

  std::sort(pix_descs.begin(), pix_descs.end(),
            [](const AVPixFmtDescriptor *a, const AVPixFmtDescriptor *b) -> bool { return strcmp(a->name, b->name) < 0; });

  const int nfields = 11;
  const char *fieldnames[11] = {
      "Name", "Alias", "NumberOfComponents", "BitsPerPixel",
      "RGB", "Alpha", "Paletted", "HWAccel", "Bayer",
      "Log2ChromaW", "Log2ChromaH"};

  plhs[0] = mxCreateStructMatrix(pix_descs.size(), 1, nfields, fieldnames);

  for (int j = 0; j < pix_descs.size(); ++j)
  {
    const AVPixFmtDescriptor *pix_fmt_desc = pix_descs[j];
    AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(pix_fmt_desc);
    mxSetField(plhs[0], j, "Name", mxCreateString(pix_fmt_desc->name));
    mxSetField(plhs[0], j, "Alias", mxCreateString(pix_fmt_desc->alias));
    mxSetField(plhs[0], j, "NumberOfComponents", mxCreateDoubleScalar(pix_fmt_desc->nb_components));
    mxSetField(plhs[0], j, "Log2ChromaW", mxCreateDoubleScalar(pix_fmt_desc->log2_chroma_w));
    mxSetField(plhs[0], j, "Log2ChromaH", mxCreateDoubleScalar(pix_fmt_desc->log2_chroma_h));
    mxSetField(plhs[0], j, "BitsPerPixel", mxCreateDoubleScalar(av_get_bits_per_pixel(pix_fmt_desc)));
    mxSetField(plhs[0], j, "Paletted", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_PAL) ? "on" : (pix_fmt_desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL) ? "pseudo" : "off"));
    mxSetField(plhs[0], j, "HWAccel", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_HWACCEL) ? "on" : "off"));
    mxSetField(plhs[0], j, "RGB", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_RGB) ? "on" : "off"));
    mxSetField(plhs[0], j, "Alpha", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_ALPHA) ? "on" : "off"));
    mxSetField(plhs[0], j, "Bayer", mxCreateString((pix_fmt_desc->flags & AV_PIX_FMT_FLAG_BAYER) ? "on" : "off"));
  }
}

mxArray *mexVideoReader::getVideoCompressions() // formats = getVideoCompressions();
{
  return ::getMediaCompressions([](const AVCodecDescriptor* desc)->bool)
  {
    return avcodec_find_decoder(desc->id) && desc->type == AVMEDIA_TYPE_VIDEO && !strstr(desc->name, "_deprecated");
  });
}

mxArray *mexVideoReader::getVideoFormats()
{
  return getVideoFormats([](const AVPixelFormat pix_fmt) -> bool {

    // supported by the IO buffers (8-bit, no subsampled components)
    if (!ffmpeg::imageCheckComponentSize(pix_fmt))
      return false;
    
    // supported by SWS library
    return sws_isSupportedInput(pix_fmt) && sws_isSupportedOutput(pix_fmt);
  });
}

// tf = isSupportedFormat(format_name);
mxArray *mexVideoReader::isSupportedFormat(const mxArray *prhs)
{
  return ::isSupportedVideoFormat(prhs, [](const AVPixelFormat pix_fmt) -> bool {
    // must <= 8-bit/component
    if (!ffmpeg::imageCheckComponentSize(pix_fmt))
      return false;

    // supported by SWS library
    return sws_isSupportedInput(pix_fmt) && sws_isSupportedOutput(pix_fmt);
  });
}

AVPixelFormat mexVideoReader::mexArrayToFormat(const mxArray *obj)
{
  return ::mexArrayToFormat(obj,[](const AVPixelFormat pix_fmt)->bool 
  { return mexImageFilter::isSupportedFormat(pix_fmt); } );
}

// validateSARString(SAR_expression);
void mexVideoReader::validateSARString(const mxArray *prhs)
{
  AVRational sar = mexParseRatio(prhs);
  if (sar.num <= 0 || sar.den <= 0)
    mexErrMsgTxt("SAR expression must result in a positive rational number.");
}

AVRational mexVideoReader::mexArrayToSAR(const mxArray *mxSAR)
{
  if (mxIsChar(mxSAR))
  {
    return mexParseRatio(mxSAR);
  }
  else if (mxIsScalar(mxSAR))
  {
    return av_d2q(mxGetScalar(mxSAR), INT_MAX);
  }
  else // 2-elem
  {
    double *data = mxGetPr(mxSAR);
    return av_make_q((int)data[0], (int)data[1]);
  }
}
