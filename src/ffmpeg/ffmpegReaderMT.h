#pragma once

#include "ThreadBase.h"
#include "ffmpegAVFrameDoubleBuffer.h"
#include "ffmpegReader.h"

namespace ffmpeg
{

// single-thread media file reader
class ReaderMT : public Reader<AVFrameDoubleBufferMT>, private ThreadBase
{
  public:
  ReaderMT(const std::string &url = "") : Reader<AVFrameDoubleBufferMT>(url) {}
  ~ReaderMT() {} // may need to clean up filtergraphs

  void closeFile();

  void activate();

  /**
   * \brief Empty all the buffers and filter graph states
   */
  void flush();

  template <class Chrono_t>
  void seek(const Chrono_t t0, const bool exact_search = true);

  protected:
  /**
   * \brief Worker thread function to read frames and stuff buffers
   */
  void thread_fcn();

  /**
   * \brief Blocks until at least one previously empty read buffer becomes ready
   */
  void read_next_packet();
};

} // namespace ffmpeg
