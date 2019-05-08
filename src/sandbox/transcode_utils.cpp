#include "transcode_utils.h"

extern "C"
{
#include <libavutil/mem.h>
#include <libavutil/buffer.h>
#include <libavutil/time.h>
#include <libavutil/parseutils.h>
#include <libavutil/timestamp.h>
}

#ifdef AV_TIME_BASE_Q
#undef AV_TIME_BASE_Q
#endif
#define AV_TIME_BASE_Q AVRational({1, AV_TIME_BASE})

std::string av_ts2timestr(int64_t ts, AVRational *tb)
{
    std::string buff(AV_TS_MAX_STRING_SIZE+1, 0);
    av_ts_make_time_string(buff->data(), ts, tb);
    return buff;
}

void assert_avoptions(AVDictionary *m);
AVRational duration_max(int64_t tmp, int64_t *duration, AVRational tmp_time_base, AVRational time_base);
int64_t parse_time_or_die(const char *context, const char *timestr, int is_duration);

void assert_avoptions(AVDictionary *m)
{
    AVDictionaryEntry *t;
    if ((t = av_dict_get(m, "", NULL, AV_DICT_IGNORE_SUFFIX)))
    {
        av_log(NULL, AV_LOG_FATAL, "Option %s not found.\n", t->key);
        throw;
    }
}

// set duration to max(tmp, duration) in a proper time base and return duration's time_base
AVRational duration_max(int64_t tmp, int64_t *duration, AVRational tmp_time_base, AVRational time_base)
{
    int ret;

    if (!*duration)
    {
        *duration = tmp;
        return tmp_time_base;
    }

    ret = av_compare_ts(*duration, time_base, tmp, tmp_time_base);
    if (ret < 0)
    {
        *duration = tmp;
        return tmp_time_base;
    }

    return time_base;
}

int64_t parse_time_or_die(const char *context, const char *timestr, int is_duration)
{
    int64_t us;
    if (av_parse_time(&us, timestr, is_duration) < 0)
    {
        av_log(NULL, AV_LOG_FATAL, "Invalid %s specification for %s: %s\n",
               is_duration ? "duration" : "date", context, timestr);
        throw;
    }
    return us;
}

av_const int mid_pred(int a, int b, int c)
{
    if(a>b){
        if(c>b){
            if(c>a) b=a;
            else    b=c;
        }
    }else{
        if(b>c){
            if(c>a) b=c;
            else    b=a;
        }
    }
    return b;
}
