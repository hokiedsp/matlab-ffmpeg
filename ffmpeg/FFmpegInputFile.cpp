#include "FFmpegInputFile.h"

#include <set>

#include "ffmpeg_utils.h"
#include "avexception.h"

void FFmpegInputFile::close()
{
    if (fmt_ctx)
    {
        streams.clear();                // kill stream codecs first
        avformat_close_input(&fmt_ctx); // close file and clear fmt_ctx
    }
}

void FFmpegInputFile::open(const char *filename, AVInputFormat *iformat, AVDictionary *opts)
{
    int err, i;
    int scan_all_pmts_set = 0;

    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx)
        AVException::log_error(filename, AVERROR(ENOMEM), true);

    // if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE))
    // {
    //     av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
    //     scan_all_pmts_set = 1;
    // }
    if ((err = avformat_open_input(&fmt_ctx, filename, iformat, opts ? &opts : nullptr)) < 0)
    {
        // try search in the MATLAB path before quitting
        std::string filepath = mxWhich(filename);
        if (filepath.size())
            err = avformat_open_input(&fmt_ctx, filepath.c_str(), iformat, opts ? &opts : nullptr);
        if (err < 0) // no luck
            AVException::log_error(filename, err, true);
    }

    // fmt_ctx valid

    // fill stream information if not populated yet
    err = avformat_find_stream_info(fmt_ctx, opts ? &opts : nullptr);
    if (err < 0)
        AVException::log_error(filename, err, true);

    // if (scan_all_pmts_set)
    //     av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
    // if ((t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX)))
    // {
    //     av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
    //     return AVERROR_OPTION_NOT_FOUND;
    // }

    /* bind a decoder to each input stream */
    for (i = 0; i < (int)fmt_ctx->nb_streams; i++)
        streams.emplace_back(fmt_ctx, i, opts);
}

std::vector<std::string> FFmpegInputFile::getMediaTypes() const
{
    // create a unique list of codec types
    std::set<AVMediaType> types;
    for (int i = 0; i < (int)fmt_ctx->nb_streams; ++i)
        types.insert(fmt_ctx->streams[i]->codecpar->codec_type);

    // convert enum to string
    std::vector<std::string> ret;
    for (auto it = types.begin(); it != types.end(); ++it)
        ret.emplace_back(av_get_media_type_string(*it));
    return ret;
}

void FFmpegInputFile::dumpToMatlab(mxArray *mxInfo, const int index) const
{
    ///////////////////////////////////////////
    // MACROs to set mxArray struct fields

#define mxSetEmptyField(fname) mxSetField(mxInfo, index, (fname), mxCreateDoubleMatrix(0, 0, mxREAL))
#define mxSetScalarField(fname, fval) mxSetField(mxInfo, index, (fname), mxCreateDoubleScalar((double)(fval)))
#define mxSetStringField(fname, fval) mxSetField(mxInfo, index, (fname), mxCreateString((fval)))
#define mxSetRatioField(fname, fval)            \
    mxTMP = mxCreateDoubleMatrix(1, 2, mxREAL); \
    pr = mxGetPr(mxTMP);                        \
    pr[0] = (fval).num;                         \
    pr[1] = (fval).den;                         \
    mxSetField(mxInfo, index, (fname), mxTMP)

    mxSetStringField("format", fmt_ctx->iformat->name);
    mxSetField(mxInfo, index, "metadata", mxCreateTags(fmt_ctx->metadata));

    if (fmt_ctx->duration != AV_NOPTS_VALUE)
    {
        int64_t duration = fmt_ctx->duration + (fmt_ctx->duration <= INT64_MAX - 5000 ? 5000 : 0);
        mxSetScalarField("duration_ts", duration);
        mxSetScalarField("duration", duration / (double)AV_TIME_BASE);
    }
    else
    {
        mxSetStringField("duration_ts", "N/A");
        mxSetStringField("duration", "N/A");
    }
    if (fmt_ctx->start_time != AV_NOPTS_VALUE)
    {
        mxSetScalarField("start_ts", fmt_ctx->start_time);
        mxSetScalarField("start", llabs(fmt_ctx->start_time / (double)AV_TIME_BASE));
    }
    else
    {
        mxSetEmptyField("start_ts");
        mxSetEmptyField("start");
    }
    if (fmt_ctx->bit_rate)
        mxSetScalarField("bitrate", fmt_ctx->bit_rate);
    else
        mxSetStringField("bitrate", "N/A");

    mxArray *mxChapters = createMxChapterStruct(fmt_ctx->nb_chapters);
    mxSetField(mxInfo, index, "chapters", mxChapters);
    for (int i = 0; i < (int)fmt_ctx->nb_chapters; i++)
    {
        AVChapter *ch = fmt_ctx->chapters[i];
        mxSetField(mxChapters, i, "start", mxCreateDoubleScalar(ch->start * av_q2d(ch->time_base)));
        mxSetField(mxChapters, i, "end", mxCreateDoubleScalar(ch->end * av_q2d(ch->time_base)));
        mxSetField(mxChapters, i, "metadata", mxCreateTags(ch->metadata));
    }

    std::vector<bool> notshown(fmt_ctx->nb_streams, true);
    int total = 0; // total streams in programs

    mxArray *mxPrograms = createMxProgramStruct(fmt_ctx->nb_programs);
    mxSetField(mxInfo, index, "programs", mxPrograms);
    if (fmt_ctx->nb_programs)
    {
        int j, k;
        for (j = 0; j < (int)fmt_ctx->nb_programs; j++)
        {
            AVDictionaryEntry *name = av_dict_get(fmt_ctx->programs[j]->metadata, "name", NULL, 0);

            mxSetField(mxPrograms, j, "id", mxCreateDoubleScalar(fmt_ctx->programs[j]->id));
            mxSetField(mxPrograms, j, "name", mxCreateString(name ? name->value : ""));
            mxSetField(mxPrograms, j, "metadata", mxCreateTags(fmt_ctx->programs[j]->metadata));

            mxArray *mxStreams = FFmpegInputStream::createMxInfoStruct(fmt_ctx->programs[j]->nb_stream_indexes);
            for (k = 0; k < (int)fmt_ctx->programs[j]->nb_stream_indexes; k++)
            {
                streams[fmt_ctx->programs[j]->stream_index[k]].dumpToMatlab(mxStreams, k);
                notshown[fmt_ctx->programs[j]->stream_index[k]] = false;
            }
            total += fmt_ctx->programs[j]->nb_stream_indexes;
        }
    }

    mxArray *mxStreams = FFmpegInputStream::createMxInfoStruct(fmt_ctx->nb_streams - total);
    mxSetField(mxInfo, index, "streams", mxStreams);
    int j = 0;
    for (int i = 0; i < (int)fmt_ctx->nb_streams; i++)
        if (notshown[i])
            streams[i].dumpToMatlab(mxStreams, j++);
}

#define ARRAY_LENGTH(_array_) (sizeof(_array_) / sizeof(_array_[0]))

mxArray *FFmpegInputFile::createMxInfoStruct(mwSize size)
{
    return mxCreateStructMatrix(size, 1, ARRAY_LENGTH(field_names), field_names);
}

mxArray *FFmpegInputFile::createMxChapterStruct(mwSize size)
{
    return mxCreateStructMatrix(size, 1, ARRAY_LENGTH(chapter_field_names), chapter_field_names);
}

mxArray *FFmpegInputFile::createMxProgramStruct(mwSize size)
{
    return mxCreateStructMatrix(size, 1, ARRAY_LENGTH(program_field_names), program_field_names);
}

const char *FFmpegInputFile::field_names[] = {
    "format",
    "metadata",
    "duration_ts",
    "duration",
    "start_ts",
    "bitrate",
    "start",
    "streams",
    "chapters",
    "programs"};

const char *FFmpegInputFile::chapter_field_names[] = {
    "start",
    "end",
    "metadata"};

const char *FFmpegInputFile::program_field_names[] = {
    "id",
    "name",
    "metadata",
    "streams"};
