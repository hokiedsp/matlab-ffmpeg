#include "mexImageFilter.h"

mexImageFilter::~mexImageFilter() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

void mexImageFilter::set_prop(const std::string name, const mxArray *value)
{
  if (name == "FilterGraph") // integer between -10 and 10
  {
    if (!(mxIsNumeric(value) && mxIsScalar(value)) || mxIsComplex(value))
      throw 0;
  }
  else
  {
    throw std::runtime_error(std::string("Unknown property name:") + name);
  }
}

mxArray *mexImageFilter::get_prop(const std::string name)
{
  mxArray *rval;
  if (name == "FilterGraph") // integer between -10 and 10
  {
    rval = mxCreateString(filtergraph.getFilterGraphDesc().c_str());
  }
  else if (name == "InputNames")
  {
    string_vector inputs = filtergraph.getInputNames();
    rval = mxCreateCellMatrix(inputs.size(), 1);
    for (int i = 0; i < inputs.size(); ++i)
      mxSetCell(rval,i,mxCreateString(inputs[i].c_str()));
  }
  else if (name == "OutputNames")
  {
    string_vector outputs = filtergraph.getOutputNames();
    rval = mxCreateCellMatrix(outputs.size(), 1);
    for (int i = 0; i < outputs.size(); ++i)
      mxSetCell(rval, i, mxCreateString(outputs[i].c_str()));
  }
  else
  {
    throw std::runtime_error(std::string("Unknown property name:") + name);
  }
  return rval;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool mexImageFilter::action_handler(const mxArray *mxObj, const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  // try the base class action (set & get) first, returns true if action has been performed
  if (mexFunctionClass::action_handler(mxObj, command, nlhs, plhs, nrhs, prhs))
    return true;

  if (command == "runSimple")
    runSimple(nlhs, plhs, nrhs, prhs);
  else if (command == "runComplex")
    runComplex(nlhs, plhs, nrhs, prhs);
  else if (command == "reset")
    reset();
  else if (command == "isSimple")
    plhs[0] = mxCreateLogicalScalar(filtergraph.isSimple());
  return true;
}

void mexImageFilter::runSimple(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
}

void mexImageFilter::runComplex(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
}

void mexImageFilter::reset()
{

}

void mexImageFilter::init(const std::string &new_graph)
{
  // release data in buffers
  for (auto src = sources.begin(); src<sources.end(); ++src) // release previously allocated resources
    src->reset();
  for (auto sink = sinks.begin(); sink<sinks.end(); ++sink) // release previously allocated resources
    sink->reset();

  // create the new graph (automatically destroys previous one)
  filtergraph.parse(new_graph);

  // create additional source & sink buffers and assign'em to filtergraph's named input & output pads
  string_vector ports = filtergraph.getInputNames();
  sources.reserve(ports.size());
  for (size_t i = sources.size(); i < ports.size(); ++i) // new source
    sources.emplace_back();
  for (size_t i = 0; i < ports.size(); ++i) // new source
    filtergraph.assignSource(ports[i], sources[i]);

  ports = filtergraph.getOutputNames();
  sinks.reserve(ports.size());
  for (size_t i = sinks.size(); i < sinks.size(); ++i) // new source
    sinks.emplace_back();
  for (size_t i = 0; i < sinks.size(); ++i) // new source
    filtergraph.assignSink(ports[i], sinks[i]);

  // configure
  filtergraph.configure();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool mexImageFilter::static_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  if (command == "getFilters")
  {
    mexImageFilter::getFilters(nlhs, plhs);
  }
  else if (command == "getVideoFormats")
  {
    mexImageFilter::getVideoFormats(nlhs, plhs);
  }
  else
    return false;
  return true;
}

void mexImageFilter::getFilters(int nlhs, mxArray *plhs[])
{
}
void mexImageFilter::getVideoFormats(int nlhs, mxArray *plhs[])
{
}
