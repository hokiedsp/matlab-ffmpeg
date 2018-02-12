#pragma once

#include "mexGetString.h" // to access mexGetString() converter
#include "mexRuntimeError.h"

#include <mex.h>

extern "C" {
#include <libavutil/parseutils.h>
}

#include <array>
#include <ctime>
#include <chrono>

/**
 * Parse str and return the parsed ratio.
 *
 * Note that a ratio with infinite (1/0) or negative value is
 * considered valid, so you should check on the returned value if you
 * want to exclude those values.
 *
 * The undefined value can be expressed using the "0:0" string.
 *
 * @param[in] mxStr the char-type mexArray to parse: it has to be a string in the format
 * num:den, a float number or an expression
 * @return the AVRational which contains the parsed ratio
 * @throws mexRuntimeError if failed to convert
 */
AVRational mexParseRatio(const mxArray *mxStr)
{
  std::string str = mexGetString(mxStr);
  AVRational rval;
  if (av_parse_ratio(&rval, str.c_str(), INT_MAX, 0, NULL)<0)
    throw mexRuntimeError("invalidRatio","Invalid expression to convert to AVRational type.");
  return rval;
}

/**
 * Parse str to get the values for width and height of a video frame.
 *
 * @param[out] height the detected height value
 * @param[out] width the detected width value
 * @param[in] mxStr the char-type mexArray to parse: it has to be a string in the format
 * width x height or a valid video size abbreviation.
 * @param[in] swap An optional flag to swap detected width and height values to account
 *                 for the column-major MATLAB data to row-major FFmpeg frame
 * @throws mexRuntimeError if failed to convert
 */
void mexParseVideoSize(int &width, int &height, const mxArray *mxStr, const bool swap = false)
{
  std::string str = mexGetString(mxStr);
  if (av_parse_video_size(&width, &height, str.c_str()) < 0)
    throw mexRuntimeError("invalidVideoSize", "Invalid expression for video size.");
  if (swap)
    std::swap(width, height);
}

/**
 * Parse str and store the detected values in *rate.
 *
 * @param[in] mxStr the char-type mexArray to parse: it has to be a string in the format
 * rate_num / rate_den, a float number or a valid video rate abbreviation
 * @returns the AVRational which will contain the detected frame rate
 * @throws mexRuntimeError if parsing fails
 */
AVRational mexParseVideoRate(const mxArray *mxStr)
{
  std::string str = mexGetString(mxStr);
  AVRational rval;
  if (av_parse_video_rate(&rval, str.c_str())<0)
    throw mexRuntimeError("invalidVideoRate", "Invalid expression for video rate.");
  return rval;
}

/** 4-element array containing RGBA color values*/
#ifndef RGBA_DEF
#define RGBA_DEF
typedef std::array<uint8_t, 4> RGBA_t;
#endif

/**
 * Parse the RGBA values that correspond to color_string.
 *
 * @param[in] mxStr the char-type mexArray to parse: It can be the name of
 *                  a color (case insensitive match) or a [0x|#]RRGGBB[AA] sequence,
 *                  possibly followed by "@" and a string representing the alpha
 *                  component.
 *                  The alpha component may be a string composed by "0x" followed by an
 *                  hexadecimal number or a decimal number between 0.0 and 1.0, which
 *                  represents the opacity value (0x00/0.0 means completely transparent,
 *                  0xff/1.0 completely opaque).
 *                  If the alpha component is not specified then 0xff is assumed.
 *                  The string "random" will result in a random color.
 * @return parsed RGBA values
 * @throws mexRuntimeError if parsing fails
 */
RGBA_t mexParseColor(const mxArray *mxStr)
{
  std::string str = mexGetString(mxStr);
  std::array<uint8_t, 4> rgba_color;
  if (av_parse_color(rgba_color.data(), str.c_str(), -1, NULL) < 0)
    throw mexRuntimeError("invalidColor", "Invalid expression for color.");
  return rgba_color;
}

/**
 * Parse the date and time values.
 *
 * @param[in] mxStr the char-type mexArray to parse given the syntax: 
 *                  @code
 *                    [{YYYY-MM-DD|YYYYMMDD}[T|t| ]]{{HH:MM:SS[.m...]]]}|{HHMMSS[.m...]]]}}[Z]
 *                    now
 *                  @endcode
 *                  If the value is "now" it takes the current time.
 *                  Time is local time unless Z is appended, in which case it is interpreted as UTC.
 *                  If the year-month-day part is not specified it takes the current year-month-day.
 * @returns the parsed high-res time_point corresponding to the string in timestr.
 * @throws mexRuntimeError if parsing fails
 */
std::chrono::high_resolution_clock::time_point mexParseTime(const mxArray *mxStr)
{
  std::string str = mexGetString(mxStr);
  int64_t uptime;
  if (av_parse_time(&uptime, str.c_str(), 0) < 0)
    throw mexRuntimeError("invalidTime", "Invalid expression for time.");

  using time_point = std::chrono::high_resolution_clock::time_point;
  return time_point(std::chrono::duration_cast<time_point::duration>(std::chrono::microseconds(uptime)));
}

/**
 * Parse duration.
 *
 * @param[in] mxStr the char-type mexArray to parse given the syntax: 
 *                  @code
 *                    [-][HH:]MM:SS[.m...]
 *                    [-]S+[.m...]
 *                  @endcode
 * @returns the microsecond-based duration
 * @throws mexRuntimeError if parsing fails
 */
std::chrono::microseconds mexParseDuration(const mxArray *mxStr)
{
  std::string str = mexGetString(mxStr);
  int64_t uptime;
  if (av_parse_time(&uptime, str.c_str(), 1) < 0)
    throw mexRuntimeError("invalidDuration", "Invalid expression for duration.");
  return std::chrono::microseconds(uptime);
}
