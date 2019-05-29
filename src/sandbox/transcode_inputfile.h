#pragma once

extern "C"
{
#include <libavformat/avformat.h>
}

#include "transcode_inputstream.h"

struct InputFile
{
    static AVCodecID default_video_codec;
    static AVCodecID default_audio_codec;
    static AVCodecID default_subtitle_codec;
    static AVCodecID default_data_codec;

    static bool find_stream_info;  // false to skip running avformat_find_stream_info() during initialization
    static bool scan_all_pmts_set; // true to scan all PMTs
    static bool bitexact;

    InputFile(const char *filename, AVInputFormat *file_iformat = NULL, bool open_decoders = true,
              AVDictionary **format_opts = NULL, AVDictionary **codec_opts = NULL);

    void findStreamInfo(AVDictionary **codec_opts = NULL);
    void openStreamDecoders();

    int process_input();
    int get_input_packet(AVPacket *pkt);
    int seek_to_start();

    int index; // unique index for each file object
    // std::vector<InputStream*> streams; /// Vector of media streams found in the file
    std::vector<InputStream> streams;

    // custom iterator
    template <AVMediaType type>
    class StreamIterator
    {
    public:
        StreamIterator(InputStreamVect::iterator begin, InputStreamVect::iterator end1) : it(begin), end(end1)
        {
            if (it != end || it->dec->type != type)
                operator++();
        }
        bool operator!=(const StreamIterator &other) { return other.it != this->it; }
        InputStream &operator*() { return *it; }
        InputStream *operator->() const { return it.operator->(); }
        StreamIterator operator++()
        {
            while (++it != end || it->dec->type != type)
                ;
            return StreamIterator(it, end);
        }

    private:
        InputStreamVect::iterator it;
        InputStreamVect::iterator end;
    };

    InputStreamVect::iterator streamBegin() { return streams.begin(); }
    InputStreamVect::iterator streamEnd() { return streams.end(); }
    StreamIterator<AVMEDIA_TYPE_VIDEO> videoStreamBegin() { return StreamIterator<AVMEDIA_TYPE_VIDEO>(streams.begin(), streams.end()); }
    StreamIterator<AVMEDIA_TYPE_VIDEO> videoStreamEnd() { return StreamIterator<AVMEDIA_TYPE_VIDEO>(streams.end(), streams.end()); }
    StreamIterator<AVMEDIA_TYPE_AUDIO> audioStreamBegin() { return StreamIterator<AVMEDIA_TYPE_AUDIO>(streams.begin(), streams.end()); }
    StreamIterator<AVMEDIA_TYPE_AUDIO> audioStreamEnd() { return StreamIterator<AVMEDIA_TYPE_AUDIO>(streams.end(), streams.end()); }
    StreamIterator<AVMEDIA_TYPE_SUBTITLE> subtitleStreamBegin() { return StreamIterator<AVMEDIA_TYPE_SUBTITLE>(streams.begin(), streams.end()); }
    StreamIterator<AVMEDIA_TYPE_SUBTITLE> subtitleStreamEnd() { return StreamIterator<AVMEDIA_TYPE_SUBTITLE>(streams.end(), streams.end()); }

    // accessed from transcode.cpp
    AVFormatContext *ctx;
    int eof_reached; /* true if eof reached */
    int eagain;      /* true if last read attempt returned EAGAIN */
    // int ist_index;   /* index of first stream in input_streams */
    // int nb_streams;  /* number of stream that ffmpeg is aware of; may be different */
    int rate_emu;

    int loop;                /* set number of times input stream should be looped */
    int64_t duration;        /* actual duration of the longest stream in a file
                             at the moment when looping happens */
    AVRational time_base;    /* time base of the duration */
    int64_t input_ts_offset; // OF

    int64_t ts_offset;
    int64_t last_ts;
    int64_t start_time; /* FI user-specified start time in AV_TIME_BASE or AV_NOPTS_VALUE */
    // int seek_timestamp;
    int64_t recording_time; // FI
    //                          from ctx.nb_streams if new streams appear during av_read_frame() */
    int nb_streams_warn; /* number of streams that the user was warned of */
    int accurate_seek;   // FI

private:
    std::string filename;
    // AVCodec *choose_decoder(OptionsContext *o, AVFormatContext *s, AVStream *st);
    // void add_input_streams(OptionsContext *o);
};

int process_input(int file_index);
