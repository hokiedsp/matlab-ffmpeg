#pragma once

#include <memory>
#include <string>
#include <vector>

#include <mex.h>

extern "C"
{
#include <libavutil/dict.h>
}

#define number_of_elements_in_array(myarray) (sizeof(myarray) / sizeof(myarray[0]))

#define mxAutoFree(p) std::unique_ptr<void, decltype(&mxFree)> cleanup_##p(p, &mxFree) // auto-deallocate the buffer

#ifdef MATLAB_PRE_R2015A
#define mxArrayToUTF8String mxArrayToString
inline bool mxIsScalar(const mxArray *array_ptr) { return mxGetNumberOfElements(array_ptr) == 1; }
#endif

/**
 * Returns 2-column Matlab cell array with the AVDicationary key names on the
 * first column and their values on the second column.
 */
mxArray *mxCreateTags(AVDictionary *tags);

/**
 * Call Matlab to look for the specified file in its search path. If found,
 * the full path to the file is returned.
 */
std::string mxWhich(const std::string &filename);

/**
 * Parse mxArray array until its element is exhausted or not character.
 * 
 * @param[in] narg  Number of arguments in args array
 * @param[in] args  Array of mxArray
 * @param[in] inc   Every inc-th input mxArray are processed (default: 1)
 * @param[in] lower True to convert characters to lower cases (default: false)
 * @returns vector of strings
 */
std::vector<std::string> mxParseStringArgs(const int narg, const mxArray *args[], int inc = 1, bool lower = false);

/**
 * Retrieve string from an mxArray
 * 
 * @param[in] arg  Array of mxArray
 * @param[in] lower True to convert characters to lower cases (default: false)
 * @returns a string
 */
std::string mxArrayToStdString(const mxArray *array, bool lower = false);

/**
 * 
 */
// void mxArrayFromAVFrame(mxArray *array, AVFrame *frame);
