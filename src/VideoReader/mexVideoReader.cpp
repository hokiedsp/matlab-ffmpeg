#include "mexVideoReader.h"

#include <algorithm>

extern "C" {
#include <libavutil/frame.h> // for AVFrame
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <cstdarg>

#include <fstream>
static std::mutex lockfile;
static std::ofstream of("mextest.csv");
#define output(command)                     \
  \
{                                        \
    std::unique_lock<std::mutex>(lockfile); \
    of << command << std::endl;             \
  \
}

// // append transpose filter at the end to show the output in the proper orientation in MATLAB

void mexFFmpegCallback(void *avcl, int level, const char *fmt, va_list argptr)
{
  if (level <= AV_LOG_VERBOSE)//AV_LOG_FATAL || level == AV_LOG_ERROR)
  {
    char dest[1024 * 16];
    vsprintf(dest, fmt, argptr);
    mexPrintf(dest);
  }
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{

  av_log_set_callback(&mexFFmpegCallback);

  mexClassHandler<mexVideoReader>(nlhs, plhs, nrhs, prhs);
}

// static
std::string mexVideoReader::mex_get_filterdesc(const mxArray *prhs)
{
  std::string tr_filter_descr("transpose=dir=0");
  if (!prhs)
    return tr_filter_descr;

  std::string filter_descr = mexGetString(prhs);

  if (filter_descr.size())
    tr_filter_descr = "," + tr_filter_descr;

  return filter_descr + tr_filter_descr;
}

AVPixelFormat mexVideoReader::mex_get_pixfmt(const std::string &pix_fmt_str)
{
  // check for special cases
  if (pix_fmt_str=="RGB24")
    return AV_PIX_FMT_GBRP; // planar GBR 4:4:4 24bpp;
  else if (pix_fmt_str=="Grayscale")
    return AV_PIX_FMT_GRAY8; //        Y        ,  8bpp

  AVPixelFormat pix_fmt = av_get_pix_fmt(pix_fmt_str.c_str());
  if (pix_fmt==AV_PIX_FMT_NONE)
      mexErrMsgIdAndTxt("ffmpegVideoReader:InvalidInput","Pixel format is unknown.");
  return pix_fmt;
}

// mexVideoReader(filename, filter_desc, pix_fmt, buffersize) (all arguments  prevalidated)
mexVideoReader::mexVideoReader(int nrhs, const mxArray *prhs[])
    : pix_fmt_name((nrhs > 2) ? mexGetString(prhs[2]) : "RGB24"),
      reader(mexGetString(prhs[0]), mex_get_filterdesc((nrhs > 1) ? prhs[1] : NULL), mex_get_pixfmt(pix_fmt_name)),
      buffer_capacity(0), nb_planar(1), buffers(2), wr_buf(buffers.begin()), rd_buf(buffers.end() - 1), killnow(false)
{
  // set buffers
  // if (nrhs > 3)
  //   buffer_capacity = (int)mxGetScalar(prhs[3]);
  // if (buffer_capacity == 0) // default
  //   buffer_capacity = 4;

  // pix_byte = buffer_capacity * reader.getFrameSize();

  // const AVPixFmtDescriptor &pfd = reader.getPixFmtDescriptor();
  // if (pfd.flags & AV_PIX_FMT_FLAG_PLANAR) // planar format
  //   nb_planar = pfd.nb_components;
  // else // pixel format
  //   pix_byte *= pfd.nb_components;

  // buffers[0].reset(pix_byte, nb_planar, buffer_capacity);
  // buffers[1].reset(pix_byte, nb_planar, buffer_capacity);

  // // start the reader thread
  // frame_reader = std::thread(&mexVideoReader::stuff_buffer, this);

  // accept property name-value pairs as input, throws exception if invalid property given
  if (nrhs>4)
    set_props(nrhs - 4, prhs + 4);
}

void mexVideoReader::FrameBuffer::reset(const size_t pix_byte, const size_t nb_planar, const size_t capacity)
{
  if (time)
    mxFree(time);
  if (frame)
    mxFree(frame);
  time = (double *)mxMalloc(capacity * sizeof(double));
  frame = (uint8_t *)mxMalloc(pix_byte * nb_planar * capacity);

  for (size_t n = 0; n < nb_planar; ++n)
    planes[n] = frame + n * pix_byte * capacity;

  cnt = 0;
  rd_pos = 0;
  state = DONE;
}

mexVideoReader::FrameBuffer::~FrameBuffer()
{
  if (time)
    mxFree(time);
  if (frame)
    mxFree(frame);
}

bool mexVideoReader::action_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  // try the base class action (set & get) first, returns true if action has been performed
  if (mexFunctionClass::action_handler(command, nlhs, plhs, nrhs, prhs))
    return true;

  if (command == "readFrame")
    readFrame(nlhs, plhs, nrhs, prhs);
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
      throw std::runtime_error("getFileFormats() takes not input argument.");
    mexVideoReader::getFileFormats(nlhs, plhs);
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
      reader.setCurrentTimeStamp(mxGetScalar(value));
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
  else if (name == "Path")
  {
    rval = mxCreateString(reader.getFilePath().c_str());
  }
  else if (name == "BitsPerPixel") // integer between -10 and 10
  {
    rval = mxCreateDoubleScalar(reader.getBitsPerPixel());
  }
  else if (name == "FrameRate")
  {
    rval = mxCreateDoubleScalar(reader.getFrameRate());
  }
  else if (name == "Height")
  {
    rval = mxCreateDoubleScalar((double)reader.getHeight());
  }
  else if (name == "Width")
  {
    rval = mxCreateDoubleScalar((double)reader.getWidth());
  }
  else if (name == "VideoFormat") // integer between -10 and 10
  {
    rval = mxCreateString(pix_fmt_name.c_str());
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
    rval = mxCreateDoubleScalar(reader.getCurrentTimeStamp());
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

void mexVideoReader::readFrame(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) //    varargout = readFrame(obj, varargin);
{
}
void mexVideoReader::read(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) //varargout = read(obj, varargin);
{
  throw std::runtime_error("Not yet implemented.");
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

void mexVideoReader::stuff_buffer()
{
  std::unique_lock<std::mutex> buffer_guard(buffer_lock);
  while (!killnow)
  {
    switch (wr_buf->state)
    {
    case FrameBuffer::DONE:
      buffer_guard.unlock();
      wr_buf->state = FrameBuffer::FILLING; // change the state to broadcast this buffer is now being filled

      reader.resetBuffer(buffer_capacity, wr_buf->planes, wr_buf->time);

    case FrameBuffer::FILLING: // shouldn't encounter this state

      // block until filled
      if (buffer_guard.owns_lock())
        buffer_guard.unlock();
      reader.blockTillBufferFull();

      // update the buffer state
      buffer_guard.lock();
      wr_buf->state = FrameBuffer::FILLED; // change the state to broadcast this buffer is now filled

      // swap if the other buffer already completely read
      if (rd_buf->state == FrameBuffer::DONE)
        std::swap(wr_buf, rd_buf);

      // notify the waiting reader that rd_buf now contains a filled buffer
      buffer_ready.notify_one();

    case FrameBuffer::FILLED:
    case FrameBuffer::READING:
      // wait until write buffer is ready to be written (buffer reader will swap the buffer)
      buffer_ready.wait(buffer_guard, [&]() { return (killnow || wr_buf->state == FrameBuffer::DONE); });
    }
  }
}

// [frames,timestamps] = readFrame(obj)
void mexVideoReader::readBuffer(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  size_t nb_frames;
  uint8_t *data = NULL;
  double *ts = NULL;

  // Extract the data arrays from the buffer
  {
    // wait until a buffer is ready
    std::unique_lock<std::mutex> buffer_guard(buffer_lock);
    buffer_ready.wait(buffer_guard, [&]() { return (killnow || rd_buf->state != FrameBuffer::DONE); });

    // swap away the buffer memory
    nb_frames = rd_buf->cnt;
    std::swap(data, rd_buf->frame);
    std::swap(ts, rd_buf->time);

    // replace
    rd_buf->reset(pix_byte, nb_planar, buffer_capacity);

    // if the other buffer have been fully filled, swap
    if (wr_buf->state == FrameBuffer::FILLED)
      std::swap(rd_buf, wr_buf);

    // notify the stuffer for the buffer availability
    buffer_ready.notify_one();
  }

  // create output array
  mwSize dims[5] = {reader.getNbPixelComponents(), reader.getWidth(), reader.getHeight(), 0, reader.getNbPlanar()};
    plhs[0] = mxCreateNumericArray(5, dims, mxUINT8_CLASS, mxREAL);
  dims[3] = nb_frames;
  mxSetDimensions(plhs[0], dims, 5);
  mxSetData(plhs[0], data);

  if (nlhs > 1) // create array for the time stamps if requested
  {
    plhs[1] = mxCreateDoubleMatrix(1, 0, mxREAL);
    mxSetN(plhs[1], nb_frames);
    mxSetPr(plhs[1], ts);
  }
  else // if not, free up the memory
  {
    mxFree(ts);
  }
}
