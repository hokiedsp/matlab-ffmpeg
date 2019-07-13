#include "ffmpegException.h"

#include <cstdarg>
#include <cstring>

extern "C"
{
#include <libavutil/error.h>
}

using namespace ffmpeg;

bool Exception::initialized = false;
void Exception::initialize()
{
  if (!initialized)
  {
    av_log_set_callback(Exception::log_callback);
    initialized = true;
  }
}

/////////////////////////

std::shared_mutex Exception::mutex_;
int Exception::av_throw_level = AV_LOG_FATAL;
int Exception::av_log_level = AV_LOG_INFO;
void (*Exception::log_fcn)(const std::string &line) = nullptr;
bool Exception::skip_repeated = true;
int Exception::print_prefix = 1;
int Exception::count;
std::string Exception::prev;

#define LINE_SZ 1024

void Exception::log_callback(void *ptr, int level, const char *fmt, va_list vl)
{
  char line[LINE_SZ]; // will contain the log line

  // make sure the received level only contains the log display level
  if (level >= 0) level &= 0xff;
  if (level > av_throw_level && (!log_fcn || level > av_log_level)) return;

  // format log line
  av_log_format_line2(ptr, level, fmt, vl, line, LINE_SZ, &print_prefix);

  std::unique_lock lock(mutex_);

  // if skip_repeated && the line repeated, increment the counter
  if (print_prefix && skip_repeated && !strcmp(line, prev.c_str()) && *line &&
      line[strlen(line) - 1] != '\r')
  {
    count++;
    if (level <= av_throw_level)
      throw Exception(line);
    else
      return;
  }
  prev = line; // store the last message for future comparison

  // first the exception
  if (level <= av_throw_level) throw Exception(line);

  // then the log message(s)
  log_fcn(prev);
  if (count > 0)
  {
    log_fcn(std::string("Last message repeated ") + std::to_string(count) +
            " times");
    count = 0;
  }
}
