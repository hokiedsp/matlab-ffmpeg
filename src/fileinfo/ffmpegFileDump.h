#pragma once

extern "C" {
#include <libavformat/avformat.h> // for AVFormatContext
#include <libavutil/avutil.h>     // for AVMediaType
}

#include <string>
#include <vector>
#include <utility>

#include "../Common/ffmpegBase.h"

namespace ffmpeg
{
struct FileDump : public Base
{
  typedef std::pair<int,int> Ratio_t;
  typedef std::pair<std::string, std::string> MetaDatum_t;
  typedef std::vector<MetaDatum_t> MetaData_t;

  struct SideDatum_s
  {
    std::string Type;
  };
  typedef std::vector<SideDatum_s> SideData_t;
  
  struct Dispositions_s
  {
    int8_t Default;
    int8_t Dub;
    int8_t Original;
    int8_t Comment;
    int8_t Lyrics;
    int8_t Karaoke;
    int8_t Forced;
    int8_t HearingImpaired;
    int8_t VisualImpaired;
    int8_t CleanEffects;

    Dispositions_s() : Default(-1), Dub(-1), Original(-1), Comment(-1), Lyrics(-1), Karaoke(-1), Forced(-1), HearingImpaired(-1), VisualImpaired(-1), CleanEffects(-1) {}
    };

  struct Stream_s
  {
    int ID;
    std::string Type;
    std::string CodecName;
    std::string CodecTag;
    std::string CodecProfile;
    int ReferenceFrames;

    int BitsPerRawSample;

    // Video
    std::string PixelFormat;
    std::string ColorRange;
    std::string ColorSpace;
    std::string ColorPrimaries;
    std::string ColorTransfer;
    std::string FieldOrder;
    std::string ChromaSampleLocation;
    int Width;
    int Height;
    int CodedWidth;
    int CodedHeight;
    Ratio_t SAR;
    Ratio_t DAR;
    int8_t ClosedCaption;
    int8_t Lossless;
    double AverageFrameRate;
    double RealBaseFrameRate;
    double TimeBase;
    double CodecTimeBase;

    // Audio
    int SampleRate;
    std::string ChannelLayout;
    std::string SampleFormat;
    int InitialPadding;
    int TrailingPadding;
    
    int64_t BitRate;
    int64_t MaximumBitRate;

    std::string Language;
    Dispositions_s Dispositions;
    MetaData_t MetaData;
    SideData_t SideData;

    Stream_s() : ID(-1), ReferenceFrames(-1), BitsPerRawSample(-1), Width(-1), Height(-1), CodedWidth(-1), CodedHeight(-1), SAR(-1, 1),
                 DAR(-1, 1), ClosedCaption(-1), Lossless(-1), AverageFrameRate(-1), RealBaseFrameRate(-1), TimeBase(-1), CodecTimeBase(-1),
                 SampleRate(-1), InitialPadding(-1), TrailingPadding(-1), BitRate(-1), MaximumBitRate(-1) {}
    };
  typedef std::vector<Stream_s> Streams_t;

  struct Chapter_s
  {
    double StartTime;
    double EndTime;
    MetaData_t MetaData;
    Chapter_s() : StartTime(NAN), EndTime(NAN) {}
  };
  typedef std::vector<Chapter_s> Chapters_t;

  struct Program_s
  {
    int ID;
    std::string Name;
    MetaData_t MetaData;
    std::vector<int> StreamIndices;
    Program_s() : ID(-1) {}
  };
  typedef std::vector<Program_s> Programs_t;
  
  FileDump(const std::string &url);
  ~FileDump();

  std::string URL;
  std::string Format;
  MetaData_t MetaData;
  double Duration;
  double StartTime;
  int64_t BitRate;

  Chapters_t Chapters;
  Programs_t Programs;
  Streams_t Streams;

private:
  void open_file(const std::string &url);

  void dump_format();
  Stream_s dump_stream_format(AVStream *st);
  static Stream_s dump_codec(AVStream *st);
  static MetaData_t dump_metadata(AVDictionary *metadata);
  static SideData_t dump_sidedata(AVStream *st);

  AVFormatContext *ic;
};
}
