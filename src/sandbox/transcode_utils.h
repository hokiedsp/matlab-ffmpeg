#pragma once

#include <string>

extern "C"
{
#include <libavutil/dict.h>
#include <libavutil/rational.h>
}

// redefine C compound literal expression in C++
#ifdef AV_TIME_BASE_Q
#undef AV_TIME_BASE_Q
#endif
#define AV_TIME_BASE_Q AVRational({1, AV_TIME_BASE})

#ifdef av_ts2timestr
#undef av_ts2timestr
#endif
inline std::string av_ts2timestr(int64_t ts, AVRational *tb);

void hw_device_free_all(void);
void assert_avoptions(AVDictionary *m);
AVRational duration_max(int64_t tmp, int64_t *duration, AVRational tmp_time_base, AVRational time_base); // from transcode_inputfile.cpp
av_const int mid_pred(int a, int b, int c);
int64_t parse_time_or_die(const char *context, const char *timestr, int is_duration);
