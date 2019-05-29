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

AVDictionary *filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id,
                                AVFormatContext *s, AVStream *st, AVCodec *codec)
{
    AVDictionary    *ret = NULL;
    AVDictionaryEntry *t = NULL;
    int            flags = s->oformat ? AV_OPT_FLAG_ENCODING_PARAM
                                      : AV_OPT_FLAG_DECODING_PARAM;
    char          prefix = 0;
    const AVClass    *cc = avcodec_get_class();

    if (!codec)
        codec            = s->oformat ? avcodec_find_encoder(codec_id)
                                      : avcodec_find_decoder(codec_id);

    switch (st->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        prefix  = 'v';
        flags  |= AV_OPT_FLAG_VIDEO_PARAM;
        break;
    case AVMEDIA_TYPE_AUDIO:
        prefix  = 'a';
        flags  |= AV_OPT_FLAG_AUDIO_PARAM;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        prefix  = 's';
        flags  |= AV_OPT_FLAG_SUBTITLE_PARAM;
        break;
    }

    while (t = av_dict_get(opts, "", t, AV_DICT_IGNORE_SUFFIX)) {
        char *p = strchr(t->key, ':');

        /* check stream specification in opt name */
        if (p)
            switch (check_stream_specifier(s, st, p + 1)) {
            case  1: *p = 0; break;
            case  0:         continue;
            default:         exit_program(1);
            }

        if (av_opt_find(&cc, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) ||
            !codec ||
            (codec->priv_class &&
             av_opt_find(&codec->priv_class, t->key, NULL, flags,
                         AV_OPT_SEARCH_FAKE_OBJ)))
            av_dict_set(&ret, t->key, t->value, 0);
        else if (t->key[0] == prefix &&
                 av_opt_find(&cc, t->key + 1, NULL, flags,
                             AV_OPT_SEARCH_FAKE_OBJ))
            av_dict_set(&ret, t->key + 1, t->value, 0);

        if (p)
            *p = ':';
    }
    return ret;
}