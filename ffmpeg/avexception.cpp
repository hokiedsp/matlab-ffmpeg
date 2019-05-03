#include "avexception.h"

#include <cstring>

extern "C"
{
#include <libavutil/error.h>
}

bool AVException::initialized = false;
void AVException::initialize()
{
    if (!initialized)
    {
        av_log_set_callback(AVException::log_callback);
        initialized = true;
    }
}

void AVException::force_throw() // throw exception with the previous log
{
    std::shared_lock lock(mutex_);
    if (prev.size())
        throw AVException(prev.c_str());
}

void AVException::log_error(const char *filename, int err, bool fatal)
{
    char errbuf[128];
    const char *errbuf_ptr = errbuf;

    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
        errbuf_ptr = strerror(AVUNERROR(err));
    av_log(NULL, fatal ? AV_LOG_FATAL : AV_LOG_ERROR, "%s: %s\n", filename, errbuf_ptr);
}

void AVException::log(int log_level, const char *msg)
{
    av_log(NULL, log_level, "%s\n", msg);
}

std::shared_mutex AVException::mutex_;
int AVException::av_throw_level = AV_LOG_FATAL;
int AVException::av_log_level = AV_LOG_INFO;
void (*AVException::log_fcn)(const std::string &line) = nullptr;
bool AVException::skip_repeated = true;
int AVException::print_prefix = 1;
int AVException::count;
std::string AVException::prev;

#define LINE_SZ 1024

void AVException::log_callback(void *ptr, int level, const char *fmt, va_list vl)
{
    char line[LINE_SZ]; // will contain the log line

    // make sure the received level only contains the log display level
    if (level >= 0)
        level &= 0xff;
    if (level > av_throw_level && (!log_fcn || level > av_log_level))
        return;

    // format log line
    av_log_format_line2(ptr, level, fmt, vl, line, LINE_SZ, &print_prefix);

    std::unique_lock lock(mutex_);

    // if skip_repeated && the line repeated, increment the counter
    if (print_prefix && skip_repeated && !strcmp(line, prev.c_str()) &&
        *line && line[strlen(line) - 1] != '\r')
    {
        count++;
        return;
    }
    prev = line; // store the last message for future comparison

    // first the exception
    if (level <= av_throw_level)
    {
        prev = line;
        throw AVException(line);
    }

    // then the log message(s)
    if (count > 0)
    {
        log_fcn(std::string("Last message repeated ") + std::to_string(count) + " times");
        count = 0;
    }
    log_fcn(prev);
}
