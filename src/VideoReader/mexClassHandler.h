#pragma once

#include <mex.h>
#include <stdint.h>
#include <string>
#include <cstring>
#include <typeinfo>
#include <stdexcept>

#define CLASS_HANDLE_SIGNATURE 0xFF00F0A5
template <class base>
class mexClassHandle
{
public:
  mexClassHandle(base *ptr) : ptr_m(ptr), name_m(typeid(base).name()) { signature_m = CLASS_HANDLE_SIGNATURE; }
  ~mexClassHandle()
  {
    signature_m = 0;
    delete ptr_m;
  }
  bool isValid() { return ((signature_m == CLASS_HANDLE_SIGNATURE) && !strcmp(name_m.c_str(), typeid(base).name())); }
  base *ptr() { return ptr_m; }

private:
  uint32_t signature_m;
  std::string name_m;
  base *ptr_m;
};

template <class base>
inline mxArray *convertPtr2Mat(base *ptr)
{
  mexLock();
  mxArray *out = mxCreateNumericMatrix(1, 1, mxUINT64_CLASS, mxREAL);
  *((uint64_t *)mxGetData(out)) = reinterpret_cast<uint64_t>(new mexClassHandle<base>(ptr));
  return out;
}

template <class base>
inline mexClassHandle<base> *convertMat2HandlePtr(const mxArray *in)
{
  if (mxGetNumberOfElements(in) != 1 || mxGetClassID(in) != mxUINT64_CLASS || mxIsComplex(in))
    throw std::runtime_error("Input must be a real uint64 scalar.");
  mexClassHandle<base> *ptr = reinterpret_cast<mexClassHandle<base> *>(*((uint64_t *)mxGetData(in)));
  if (!ptr->isValid())
    throw std::runtime_error("Handle not valid.");
  return ptr;
}

template <class base>
inline base *convertMat2Ptr(const mxArray *in)
{
  return convertMat2HandlePtr<base>(in)->ptr();
}

template <class base>
inline void destroyObject(const mxArray *in)
{
  delete convertMat2HandlePtr<base>(in);
  mexUnlock();
}

template <class base>
inline void destroyObject(mexClassHandle<base> *in)
{
  if (in)
  {
    delete in;
    mexUnlock();
  }
}

std::string mexGetString(const mxArray *array)
{
  // ideally use std::codecvt but VSC++ does not support this particular template specialization as of 2017
  // mxChar *str_utf16 = mxGetChars(array);
  // if (str_utf16==NULL)
  //   throw 0;
  // std::string str = std::wstring_convert<std::codecvt_utf8_utf16<mxChar>, mxChar>{}.to_bytes(str_utf16);

  mwSize len = mxGetNumberOfElements(array);
  std::string str;
  str.resize(len + 1);
  if (mxGetString(array, &str.front(), str.size()) != 0)
    throw std::runtime_error("Failed to convert MATLAB string.");
  str.resize(len); // remove the trailing NULL character
  return str;
}

template <class mexClass>
void mexClassHandler(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  std::string component_id = mexClass::get_componentid();

  // if the first argument is empty & the second is a string "static", run static member function of mexClass
  bool is_static = false;
  try
  {
    is_static = nrhs > 1 && mxIsEmpty(prhs[0]) && mexGetString(prhs[1]) == "static";
  }
  catch (...)
  {
  }
  if (is_static)
  {
    if (nrhs < 3 || !mxIsChar(prhs[2]))
      mexErrMsgIdAndTxt((component_id + ":static:functionUndefined").c_str(), "Static function not given.");

    try
    {
      if (!mexClass::static_handler(mexGetString(prhs[2]), nlhs, plhs, nrhs - 3, prhs + 3))
        mexErrMsgIdAndTxt((component_id + ":static:unknownFunction").c_str(), "Unknown static function: %s", mexGetString(prhs[2]).c_str());
    }
    catch (std::exception &e)
    {
      mexErrMsgIdAndTxt((component_id + ":static:executionFailed").c_str(), e.what());
    }
    return;
  }

  mexClassHandle<mexClass> *handle = NULL;
  if (nrhs > 0)
  {
    // attempt to convert the first argument to the object
    try
    {
      handle = convertMat2HandlePtr<mexClass>(prhs[0]);
    }
    catch (...)
    {
    }
  }

  // if the first argument is not the class object, create a new object
  if (handle == NULL)
  {
    // otherwise create a new

    if (nlhs > 1)
      mexErrMsgIdAndTxt((component_id + ":tooManyOutputArguments").c_str(), "Only one argument is returned for object construction.");

    mexClass *ptr(NULL);
    try
    {
      ptr = new mexClass(nrhs, prhs);
      if (ptr == NULL)
        mexPrintf("Constructor failed silently.\n");
    }
    catch (std::exception &e)
    {
      mexPrintf("Exception thrown by the constructor\n");
      mexErrMsgIdAndTxt((component_id + ":constructorFail").c_str(), e.what());
    }

    plhs[0] = convertPtr2Mat(ptr);
    return;
  }

  // all other cases must have at least 2 input arguments & the second argument is the action command:
  std::string command;
  try
  {
    if (nrhs < 2)
      throw 0;
    command = mexGetString(prhs[1]);
  }
  catch (...)
  {
    mexErrMsgIdAndTxt((component_id + ":missingCommand").c_str(), "Second argument (command) is not a string.");
  }

  // check for the delete command
  if (command == "delete")
  {
    destroyObject<mexClass>(handle);
    return;
  }

  // otherwise perform the object-specific (if run_action() is overloaded) action according to the given command
  else
  {
    try
    {
      if (!handle->ptr()->action_handler(command, nlhs, plhs, nrhs - 2, prhs + 2))
        mexErrMsgIdAndTxt((component_id + ":unknownCommand").c_str(), "Unknown command: %s", command.c_str());
    }
    catch (std::exception &e)
    {
      mexErrMsgIdAndTxt((component_id + ":failedCommand").c_str(), e.what());
    }
  }
}

// Base class for an internal MEX class interfaced a MATLAB class object using mexClassHandle class
class mexFunctionClass
{
public:
  static std::string get_componentid() { return "mexClassGeneric"; };

  virtual bool action_handler(const std::string &command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
  {
    if (command == "set") // (H,"set","name",value)
    {
      // check for argument counts
      if (nlhs != 0 || nrhs != 2)
        mexErrMsgIdAndTxt((get_componentid() + ":set:invalidArguments").c_str(), "Set command takes 4 input arguments and returns none.");

      // get property name
      std::string name;
      try
      {
        name = mexGetString(prhs[0]);
      }
      catch (...)
      {
        mexErrMsgIdAndTxt((get_componentid() + ":set:invalidPropName").c_str(), "Set command's third argument must be a name string.");
      }

      // run the action
      try
      {
        set_prop(name, prhs[1]);
      }
      catch (std::exception &e)
      {
        mexErrMsgIdAndTxt((get_componentid() + ":set:invalidProperty").c_str(), e.what());
      }
    }
    else if (command == "get") // value = (H,"get","name")
    {
      // check for argument counts
      if (nlhs != 1 || nrhs != 1)
        mexErrMsgIdAndTxt((get_componentid() + ":get:invalidArguments").c_str(), "Get command takes 3 input arguments and returns one.");

      // get property name
      std::string name;
      try
      {
        name = mexGetString(prhs[0]);
      }
      catch (...)
      {
        mexErrMsgIdAndTxt((get_componentid() + ":get:invalidPropName").c_str(), "Get command's third argument must be a name string.");
      }

      // run the action
      try
      {
        plhs[0] = get_prop(name);
      }
      catch (std::exception &e)
      {
        mexErrMsgIdAndTxt((get_componentid() + ":get:invalidPropName").c_str(), e.what());
      }
    }
    else // no matching command found
    {
      return false;
    }

    return true;
  }

  static bool static_handler(std::string command, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
  {
    return false;
  }

protected:
  virtual void set_prop(const std::string name, const mxArray *value) = 0;
  virtual mxArray *get_prop(const std::string name) = 0;

  virtual void set_props(int nrhs, const mxArray *prhs[])
  {
    if (nrhs % 2 != 0)
      throw std::runtime_error("Properties must be given as name-value pairs.");

    for (const mxArray **arg = prhs; arg < prhs + nrhs; ++arg)
    {
      // get property name
      std::string name;
      try
      {
        name = mexGetString(*arg++);
      }
      catch (...)
      {
        throw std::runtime_error("Property name is must be a name string.");
      }
      set_prop(name, *arg);
    }
  }
};
