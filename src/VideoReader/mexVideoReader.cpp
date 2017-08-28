#include "mexVideoReader.h"

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  mexClassHandler<mexVideoReader>(nlhs, plhs, nrhs, prhs);
}

// The class that we are interfacing to
mexVideoReader::mexVideoReader(int nrhs, const mxArray *prhs[]) : reader((nrhs > 0) ? mexGetString(prhs[0]) : "", AVMEDIA_TYPE_VIDEO)
{
  // accept property name-value pairs as input, throws exception if invalid property given
  set_props(nrhs - 1, prhs + 1);
}

bool mexVideoReader::action_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  // try the base class action (set & get) first, returns true if action has been performed
  if (mexFunctionClass::action_handler(command, nlhs, plhs, nrhs, prhs))
    return true;

  if (command == "readFrame")
    readFrame(nlhs, plhs, nrhs, prhs);
  else if (command == "hasFrame")
  {
    if (nrhs > 0)
      throw std::runtime_error("getFileFormats() takes not input argument.");
    hasFrame(nlhs, plhs);
  }
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
      reader.setPTS(mxGetScalar(value));
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
    rval = mxCreateDoubleScalar(reader.getHeight());
  }
  else if (name == "Width")
  {
    rval = mxCreateDoubleScalar(reader.getWidth());
  }
  else if (name == "VideoFormat") // integer between -10 and 10
  {
    rval = mxCreateString(reader.getVideoPixelFormat().c_str());
  }
  else if (name == "VideoCompression")
  {
    std::string name = reader.getVideoCodecName();
    std::string desc = reader.getVideoCodecDesc();
    if (desc.size())
    {
      name += " (" + desc + ')';
    }
    rval = mxCreateString(name.c_str());
  }
  else if (name == "CurrentTime")
  {
    rval = mxCreateDoubleScalar(reader.getPTS());
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
  throw std::runtime_error("Not yet implemented.");
}
void mexVideoReader::read(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) //varargout = read(obj, varargin);
{
  throw std::runtime_error("Not yet implemented.");
}

void mexVideoReader::hasFrame(int nlhs, mxArray *plhs[]) //        eof = hasFrame(obj);
{
  plhs[0] = mxCreateLogicalScalar(reader.eof());
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
