#include "mxutils.h"

#include <algorithm>

extern "C"
{
#include <libavutil/opt.h>
}

#include "avexception.h"

mxArray *mxCreateTags(AVDictionary *tags)
{
    int ntags = av_dict_count(tags);
    mxArray *mxTags = mxCreateCellMatrix(ntags, 2);

#define mxSetDict(index, tag)                           \
    mxSetCell(mxTags, index, mxCreateString(tag->key)); \
    mxSetCell(mxTags, index + ntags, mxCreateString(tag->value))

    int n = 0;
    AVDictionaryEntry *tag = NULL;
    for (int n = 0; n < ntags; ++n)
    {
        tag = av_dict_get(tags, "", tag, AV_DICT_IGNORE_SUFFIX);
        mxSetDict(n, tag);
    }

    return mxTags;
}

std::string mxWhich(const std::string &filename)
{
    // create mxArray
    mxArray *rhs = mxCreateString(filename.c_str());
    mxArray *plhs[1];
    mexCallMATLAB(1, plhs, 1, &rhs, "which");

    char *filepath = mxArrayToUTF8String(plhs[0]);
    mxAutoFree(filepath);

    return std::string(filepath);
}

std::vector<std::string> mxParseStringArgs(const int narg, const mxArray *args[], int inc, bool lower)
{
    // make sure inc is positive
    if (inc<=0) inc = 1;

    std::vector<std::string> argstrs;
    for (int n = 0; (n < narg) && (mxIsChar(args[n])); n += inc)
    {
        char *pname_str = mxArrayToUTF8String(args[n]);
        std::string pname(pname_str);
        mxFree(pname_str);
        if (lower)
            std::transform(pname.begin(), pname.end(), pname.begin(), ::tolower); // lower case
        argstrs.push_back(std::move(pname));
    }
    return argstrs;
}