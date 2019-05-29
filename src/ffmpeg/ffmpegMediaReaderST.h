#pragma once

#include "ffmpegBase.h"
// #include "ffmpegAvRedefine.h"
#include "ffmpegStreamInput.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
// #include <libavutil/pixdesc.h>
}

#include <vector>

namespace ffmpeg
{

/*
 * Single-Thread Multi-Stream Media Reader
 */
class MediaReaderST : public Base
{
public:
  /* Constructor, which opens one stream specified by type. If type omitted, the preference 
   * order on the streams are: video->audio->subtitle->data->attachment.
   * 
   * @throws if filename does not resolve to a openable media file
   * @throws if specified stream type does not exist
   */
  MediaReaderST(const std::string &filename, const AVMediaType type = AVMEDIA_TYPE_UNKNOWN);

  /* Constructor, which opens the first stream specified by index.
   * 
   * @throws if filename does not resolve to a openable media file
   * @throws if specified stream does not exist
   */
  MediaReaderST(const std::string &filename, const int index);

  /* Constructor, which opens the first stream specified by the stream specifier string.
   * 
   * @throws if filename does not resolve to a openable media file
   * @throws if specified stream does not exist
   */
  MediaReaderST(const std::string &filename, const std::string &spec);

  /* Constructor, which opens multiple streams according to the specified stream types
   * 
   * @throws if filename does not resolve to a openable media file
   * @throws if none of specified stream type exists
   * 
   * @note if any specified stream fails to open, it emits ERROR av_log
   */
  MediaReaderST(const std::string &filename, const std::vector<AVMediaType> &types);

  /* Constructor, which opens multiple streams according to the specified stream indices
   * 
   * @throws if filename does not resolve to a openable media file
   * @throws if none of specified stream exists
   * 
   * @note if any specified stream fails to open, it emits av_log ERROR
   */
  MediaReaderST(const std::string &filename, const std::vector<int> &indices);

  /* Constructor, which opens multiple streams according to the specified stream specs
   * 
   * @throws if filename does not resolve to a openable media file
   * @throws if none of specified stream type does not exist
   * 
   * @note if any specified stream fails to open, it emits ERROR av_log
   */
  MediaReaderST(const std::string &filename, const std::vector<std::string> &specs);
  virtual ~MediaReaderST();

  bool isFileOpen();
  bool EndOfFile();

// thread function: responsible to read packet and send it to ffmpeg decoder
  void setFrameReadyCallback(const int index, bool (*callback)());

  void startReading();
  void stopReading();

  int64_t getCurrentTimeStamp() const;
  void setCurrentTimeStamp(const int64_t val, const bool exact_search = true);

  std::string getFilePath() const;
  AVRational getTimeBase() const;
  int64_t getDuration() const;

protected:  
  void openFile(const std::string &filename);
  void closeFile();

  int addStream(int wanted_stream_id, int related_stream_id = -1);
  int addStream(AVMediaType type, int related_stream_id = -1);
  int addStream(std::string spec, int related_stream_id = -1);

  void clearStreams();

  
private:
  virtual void add_stream(const int id);

  AVFormatContext *fmt_ctx;           // FFmpeg format context
  std::vector<InputStream*> streams;  // media streams under decoding
  
  int64_t pts;
};
}
