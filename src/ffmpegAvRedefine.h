#pragma once

// redines C++ incompatible #defines in ffmpeg API

#include <string>

extern "C" {
//#include <libavutil/avassert.h>       // for AVDictionary
#include <libavutil/error.h>       // for AVDictionary
#include <libavutil/rational.h>       // for AVDictionary
}

#ifdef AV_TIME_BASE_Q
#undef AV_TIME_BASE_Q
#define AV_TIME_BASE_Q AVRational({1,AV_TIME_BASE})
#endif

#ifdef av_err2str
#undef av_err2str
#endif
inline std::string av_err2str(int errnum)
{
    std::string str;
    str.reserve(AV_ERROR_MAX_STRING_SIZE);
    av_make_error_string((char*)str.data(), AV_ERROR_MAX_STRING_SIZE, errnum);
    return str;
}

/**
 * assert() equivalent, that is always enabled.
 */
// #define av_assert0(cond) do {                                           \
//     if (!(cond)) {                                                      \
//         av_log(NULL, AV_LOG_PANIC, "Assertion %s failed at %s:%d\n",    \
//                AV_STRINGIFY(cond), __FILE__, __LINE__);                 \
//         abort();                                                        \
//     }                                                                   \
// } while (0)