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
  ~ReaderMT() { kill(); } // may need to clean up filtergraphs

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
   * \brief Blocks until at least one previously empty read buffer becomes ready
   */
  bool read_next_packet();

  private:
  // kill buffers
  void kill();

  /**
   * \brief Worker thread function to read frames and stuff buffers
   */
  void thread_fcn() override;
  void pause() override;
  void resume() override;
};

template <class Chrono_t>
inline void ReaderMT::seek(const Chrono_t t0, const bool exact_search)
{
  // stop thread before seek
  pause();

  // do the coarse search first
  Reader<AVFrameDoubleBufferMT>::seek<Chrono_t>(t0, false);

  // restart thread
  resume();

  // perform the exact search only after the thread has restarted
  if (exact_search) Reader<AVFrameDoubleBufferMT>::purge_until<Chrono_t>(t0);
}

} // namespace ffmpeg
