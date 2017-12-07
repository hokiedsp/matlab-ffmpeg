#include "mexVideoReader.h"

#include <algorithm>

extern "C" {
#include <libavutil/frame.h> // for AVFrame
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <cstdarg>
#include <locale>

// // append transpose filter at the end to show the output in the proper orientation in MATLAB

void mexFFmpegCallback(void *avcl, int level, const char *fmt, va_list argptr)
{
  if (level <= AV_LOG_VERBOSE)//AV_LOG_FATAL || level == AV_LOG_ERROR)
  {
    char dest[1024 * 16];
    vsprintf(dest, fmt, argptr);
    mexPrintf(dest);
    // output(dest);
  }
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{

  av_log_set_callback(&mexFFmpegCallback);

  mexClassHandler<mexVideoReader>(nlhs, plhs, nrhs, prhs);
}

// static
std::string mexVideoReader::mex_get_filterdesc(const mxArray *obj)
{
  // always appends transpose filter so that the video is presented to MATLAB columns first
  std::string tr_filter_descr("transpose=dir=0");

  std::string sc_filter_descr("");
  int w = (int)mxGetScalar(mxGetProperty(obj, 0, "Width"));
  int h = (int)mxGetScalar(mxGetProperty(obj, 0, "Height"));
  // const double* sar = mxGetPr(mxGetProperty(obj, 0, "PixelAspectRatio"));

  if (h > 0 || w > 0)
    sc_filter_descr = "scale=" + std::to_string(w) + ":" + std::to_string(h) + ",";

  std::string filter_descr = mexGetString(mxGetProperty(obj, 0, "VideoFilter"));
  if (filter_descr.size())
    filter_descr += ",";

  return filter_descr + sc_filter_descr + tr_filter_descr;
}

AVPixelFormat mexVideoReader::mex_get_pixfmt(const mxArray *obj)
{
  std::string pix_fmt_str = mexGetString(mxGetProperty(obj, 0, "VideoFormat"));

  // check for special cases
  if (pix_fmt_str == "grayscale")
    return AV_PIX_FMT_GRAY8; //        Y        ,  8bpp

  AVPixelFormat pix_fmt = av_get_pix_fmt(pix_fmt_str.c_str());
  if (pix_fmt==AV_PIX_FMT_NONE) // just in case
      mexErrMsgIdAndTxt("ffmpegVideoReader:InvalidInput","Pixel format is unknown.");

  if (!(sws_isSupportedOutput(pix_fmt) && mexComponentBuffer::supportedPixelFormat(pix_fmt)))
      mexErrMsgIdAndTxt("ffmpegVideoReader:InvalidInput","Pixel format is not supported.");
  
  return pix_fmt;
}

// mexVideoReader(mobj, filename) (all arguments  pre-validated)
mexVideoReader::mexVideoReader(int nrhs, const mxArray *prhs[])
    : killnow(false)
{
  // open the video file
  reader.openFile(mexGetString(prhs[1]), mex_get_filterdesc(prhs[0]), mex_get_pixfmt(prhs[0]));

  // set unspecified properties
  if (mxIsEmpty(mxGetProperty(prhs[0],0,"FrameRate")))
    mxSetProperty((mxArray *)prhs[0], 0, "FrameRate", mxCreateDoubleScalar((double)reader.getFrameRate()));
  
  if (mxGetScalar(mxGetProperty(prhs[0], 0, "Width"))<=0.0)
    mxSetProperty((mxArray *)prhs[0], 0, "Width", mxCreateDoubleScalar((double)reader.getHeight()));

  if (mxGetScalar(mxGetProperty(prhs[0], 0, "Height"))<=0.0)
    mxSetProperty((mxArray *)prhs[0], 0, "Height", mxCreateDoubleScalar((double)reader.getWidth()));

  if (mxIsEmpty(mxGetProperty(prhs[0], 0, "PixelAspectRatio")))
  {
    mxArray *sar = mxCreateDoubleMatrix(1, 2, mxREAL);
    *mxGetPr(sar) = (double)reader.getSAR().num;
    *(mxGetPr(sar)+1) = (double)reader.getSAR().den;
    mxSetProperty((mxArray *)prhs[0], 0, "PixelAspectRatio", sar);
  }

  // set buffers
  buffer_capacity = (int)mxGetScalar(mxGetProperty(prhs[0],0,"BufferSize"));
  buffers.reserve(2);
  buffers.emplace_back(buffer_capacity, reader.getWidth(), reader.getHeight(), reader.getPixelFormat());
  buffers.emplace_back(buffer_capacity, reader.getWidth(), reader.getHeight(), reader.getPixelFormat());
  wr_buf = buffers.begin();
  rd_buf = wr_buf + 1;

  reader.resetBuffer(&*wr_buf);

  // start the reader thread
  frame_writer = std::thread(&mexVideoReader::shuffle_buffers, this);
}

mexVideoReader::~mexVideoReader()
{
  killnow = true;

  // close the file before buffers are destroyed
  reader.closeFile();

  {
    std::unique_lock<std::mutex> buffer_guard(buffer_lock);
    buffer_ready.notify_one();
  }
  
  // start the file reading thread (sets up and idles)
  if (frame_writer.joinable())
    frame_writer.join();
}

bool mexVideoReader::action_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  // try the base class action (set & get) first, returns true if action has been performed
  if (mexFunctionClass::action_handler(command, nlhs, plhs, nrhs, prhs))
    return true;

  if (command == "readFrame")
    readFrame(nlhs, plhs, nrhs, prhs);
  else if (command == "readBuffer")
    readBuffer(nlhs, plhs, nrhs, prhs);
  else if (command == "read")
    read(nlhs, plhs, nrhs, prhs);
  else
    return false;
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
  if (command == "validate_pixfmt")
  {
    if (nrhs!=1 || !mxIsChar(prhs[0]))
      throw std::runtime_error("validate_pixfmt0() takes one string input argument.");
    
    std::string pixfmt = mexGetString(prhs[0]);
    if (av_get_pix_fmt(pixfmt.c_str()) == AV_PIX_FMT_NONE)
      mexErrMsgIdAndTxt("ffmpeg:VideoReader:validate_pixfmt:invalidFormat", "%s is not a valid FFmpeg Pixel Format", pixfmt.c_str());
  }
  else
    return false;
  return true;
}

void mexVideoReader::set_prop(const std::string name, const mxArray *value)
{
  if (name == "CurrentTime")
  {
    try
    {
      if (!(mxIsNumeric(value) && mxIsScalar(value)) || mxIsComplex(value))
        throw 0;
      
      std::unique_lock<std::mutex> buffer_guard(buffer_lock);
      // this should stop the shuffle_buffer thread when the buffer is not available

      reader.resetBuffer(NULL);

      // set new time
      reader.setCurrentTimeStamp(mxGetScalar(value));

      // reset buffers
      wr_buf->reset();
      rd_buf->reset();

      // set write buffer to reader
      reader.resetBuffer(&*wr_buf);

      // tell  shuffle_buffers thread to resume
      buffer_ready.notify_one();
    }
    catch (...)
    {
      throw std::runtime_error("VarA must be a scalar integer between -10 and 10.");
    }
  }
  else
  {
    throw std::runtime_error(std::string("Unknown property name:") + name);
  }
}

mxArray *mexVideoReader::get_prop(const std::string name)
{
  mxArray *rval;
  if (name == "Duration") // integer between -10 and 10
  {
    rval = mxCreateDoubleScalar(reader.getDuration());
  }
  else if (name == "BitsPerPixel") // integer between -10 and 10
  {
    rval = mxCreateDoubleScalar(reader.getBitsPerPixel());
  }
  else if (name == "VideoCompression")
  {
    std::string name = reader.getCodecName();
    std::string desc = reader.getCodecDescription();
    if (desc.size())
    {
      name += " (" + desc + ')';
    }
    rval = mxCreateString(name.c_str());
  }
  else if (name == "CurrentTime")
  {
    double t(NAN);

    std::unique_lock<std::mutex> buffer_guard(buffer_lock);
    if (!rd_buf->available())
      buffer_ready.wait(buffer_guard);
    rd_buf->read_frame(NULL, &t, false);
    buffer_ready.notify_one();

    rval = mxCreateDoubleScalar(t);
  }
  else if (name == "AudioCompression") // integer between -10 and 10
  {
    rval = mxCreateString("");
  }
  else if (name == "NumberOfAudioChannels")
  {
    rval = mxCreateDoubleMatrix(0, 0, mxREAL);
  }
  else if (name == "NumberOfFrames")
  {
    rval = mxCreateDoubleScalar((double)reader.getNumberOfFrames());
  }
  else
  {
    throw std::runtime_error(std::string("Unknown property name:") + name);
  }
  return rval;
}

void mexVideoReader::getFileFormats(int nlhs, mxArray *plhs[]) // formats = getFileFormats();
{
  ffmpeg::AVInputFormatPtrs ifmtptrs = ffmpeg::Base::get_input_formats_devices(AVMEDIA_TYPE_VIDEO, AVFMT_NOTIMESTAMPS);

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
    mxSetField(plhs[0], index, "is_file", mxCreateLogicalScalar(ifmtptrs[index]->flags & AVFMT_NOFILE));
    mxSetField(plhs[0], index, "need_number", mxCreateLogicalScalar(ifmtptrs[index]->flags & AVFMT_NEEDNUMBER));
    mxSetField(plhs[0], index, "show_ids", mxCreateLogicalScalar(ifmtptrs[index]->flags & AVFMT_SHOW_IDS));
    mxSetField(plhs[0], index, "generic_index", mxCreateLogicalScalar(ifmtptrs[index]->flags & AVFMT_GENERIC_INDEX));
    mxSetField(plhs[0], index, "ts_discont", mxCreateLogicalScalar(ifmtptrs[index]->flags & AVFMT_TS_DISCONT));
    mxSetField(plhs[0], index, "bin_search", mxCreateLogicalScalar(!(ifmtptrs[index]->flags & AVFMT_NOBINSEARCH)));
    mxSetField(plhs[0], index, "gen_search", mxCreateLogicalScalar(!(ifmtptrs[index]->flags & AVFMT_NOGENSEARCH)));
    mxSetField(plhs[0], index, "byte_seek", mxCreateLogicalScalar(ifmtptrs[index]->flags & AVFMT_NO_BYTE_SEEK));
    mxSetField(plhs[0], index, "seek_to_pts", mxCreateLogicalScalar(ifmtptrs[index]->flags & AVFMT_SEEK_TO_PTS));
  }
}

void mexVideoReader::shuffle_buffers()
{
  std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  while (!killnow)
  {
    if (!rd_buf->available()) // all read
    {
      // done with the read buffer
      rd_buf->reset();

      // wait until write buffer is full
      reader.blockTillBufferFull();
      if (killnow)
        break;

      // swap the buffers
      std::swap(wr_buf, rd_buf);

      // notify the waiting thread that rd_buf now contains a filled buffer
      buffer_ready.notify_one();

      // give reader the new buffer
      reader.resetBuffer(&*wr_buf);

    }
    else // read buffer still has unread frames, wait until all read
    {
      buffer_ready.wait(buffer_guard); // to be woken up by read functions
    }
  }
}

void mexVideoReader::readFrame(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) //[frame,time] = readFrame(obj, varargin);
{
  // if buffer size is 1, use readBuffer() to avoid copying the frame
  if (buffer_capacity==1)
  {
    readBuffer(nlhs,plhs,nrhs,prhs);
    return;
  }

  mwSize dims[3] = {reader.getWidth(), reader.getHeight(), reader.getPixFmtDescriptor().nb_components};
  plhs[0] = mxCreateNumericArray(3,dims,mxUINT8_CLASS,mxREAL);
  uint8_t *dst = (uint8_t*)mxGetData(plhs[0]);
  double t(NAN);

  std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  if (!rd_buf->available())
    buffer_ready.wait(buffer_guard);
  rd_buf->read_frame(dst, (nlhs > 1) ? &t : NULL);
  buffer_ready.notify_one();
  buffer_guard.unlock();

  if (nlhs>1)
    plhs[1] = mxCreateDoubleScalar(t);
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

  // Extract the data arrays from the buffer
  {
    // wait until a buffer is ready
    std::unique_lock<std::mutex> buffer_guard(buffer_lock);
    if (!rd_buf->full())
      buffer_ready.wait(buffer_guard);

  // release the buffer data (buffer automatically reallocate new data block)
    nb_frames = rd_buf->release(&data, &ts);

  // notify the stuffer for the buffer availability
    buffer_ready.notify_one();
  }


  // create output array
  mwSize dims[4] = {reader.getWidth(), reader.getHeight(), reader.getPixFmtDescriptor().nb_components, 0};
  av_log(NULL,AV_LOG_INFO,"mexVideoReader::readBuffer::frame data size obtained: %d, %d, %d\n",dims[0],dims[1],dims[2]);
  plhs[0] = mxCreateNumericArray(4, dims, mxUINT8_CLASS, mxREAL);
  av_log(NULL,AV_LOG_INFO,"mexVideoReader::readBuffer::empty mxArray created\n");
  dims[3] = nb_frames;
  mxSetDimensions(plhs[0], dims, 4);
  av_log(NULL,AV_LOG_INFO,"mexVideoReader::readBuffer::assigned mxArray size\n");
  mxSetData(plhs[0], data);
  av_log(NULL,AV_LOG_INFO,"mexVideoReader::readBuffer::mxArray frame data array created\n");

  if (nlhs > 1) // create array for the time stamps if requested
  {
    plhs[1] = mxCreateDoubleMatrix(1, 0, mxREAL);
    mxSetN(plhs[1], nb_frames);
    mxSetPr(plhs[1], ts);
  av_log(NULL,AV_LOG_INFO,"mexVideoReader::readBuffer::mxArray time array created\n");
  }
  else // if not, free up the memory
  {
    mxFree(ts);
  }
}

  // else if (name == "FrameRate")
  // {
  //   rval = mxCreateDoubleScalar(reader.getFrameRate());
  // }
  // else if (name == "Height")
  // {
  //   rval = mxCreateDoubleScalar((double)reader.getHeight());
  // }
  // else if (name == "Width")
  // {
  //   rval = mxCreateDoubleScalar((double)reader.getWidth());
  // }
