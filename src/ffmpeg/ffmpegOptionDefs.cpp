#include "ffmpegOptionDefs.h"

extern "C" {
#include "libavutil/opt.h"
}

using namespace ffmpeg;

OptionDefs &add_io_options(OptionDefs &defs) // append option definitions that are common to both input/output files
{
   // include INPUT/OUTPUT OPTIONS by default
   defs.insert(defs.end(), {
                               {"codec", OPT_INPUT | OPT_OUTPUT | HAS_ARG | OPT_STRING | OPT_SPEC, "codec name", "codec"}, // codec_names
                               {"c", OPT_INPUT | OPT_OUTPUT | IS_ALIAS, "codec", ""},
                               {"f", OPT_INPUT | OPT_OUTPUT | HAS_ARG | OPT_STRING, "force format", "fmt"},                                               // format
                               {"ss", OPT_INPUT | OPT_OUTPUT | HAS_ARG | OPT_TIME, "set the start time offset", "time_off"},                              // start_time
                               {"sseof", OPT_INPUT | OPT_OUTPUT | HAS_ARG | OPT_TIME, "set the start time offset relative to EOF", "time_off"},           //start_time_eof
                               {"t", OPT_INPUT | OPT_OUTPUT | HAS_ARG | OPT_TIME, "record or transcode \"duration\" seconds of audio/video", "duration"}, //recording_time
                               {"tag", OPT_OUTPUT | OPT_INPUT | HAS_ARG | OPT_STRING | OPT_SPEC, "force codec tag/fourcc", "fourcc/tag"},                 // codec_tags

                               {"pix_fmt", OPT_INPUT | OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_SPEC, "set pixel format", "format"},                            // frame_pix_fmts
                               {"r", OPT_INPUT | OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_SPEC, "set frame rate (Hz value, fraction or abbreviation)", "rate"}, // frame_rates
                               {"s", OPT_INPUT | OPT_OUTPUT | OPT_VIDEO | OPT_SUBTITLE | HAS_ARG | OPT_STRING | OPT_SPEC, "set frame size (WxH or abbreviation)", "size"}, // frame_sizes
                               {"top", OPT_INPUT | OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_INT | OPT_SPEC, "top=1/bottom=0/auto=-1 field first", ""},                       // top_field_first
                               {"vcodec", OPT_INPUT | OPT_OUTPUT | OPT_VIDEO | IS_ALIAS, "codec:v", ""},                                                                   // called opt_video_codec()
                               {"vn", OPT_INPUT | OPT_OUTPUT | OPT_VIDEO | OPT_BOOL, "disable video"},                                                                     //video_disable

                               {"ar", OPT_INPUT | OPT_OUTPUT | OPT_AUDIO | HAS_ARG | OPT_INT | OPT_SPEC, "set audio sampling rate (in Hz)", "rate"},        // audio_sample_rate
                               {"ac", OPT_INPUT | OPT_OUTPUT | OPT_AUDIO | HAS_ARG | OPT_INT | OPT_SPEC, "set number of audio channels", "channels"},       // audio_channels
                               {"an", OPT_INPUT | OPT_OUTPUT | OPT_AUDIO | OPT_BOOL, "disable audio"},                                                      // audio_disable
                               {"acodec", OPT_INPUT | OPT_OUTPUT | OPT_AUDIO | IS_ALIAS, "codec:a", ""},                                                    // called opt_audio_codec()
                               {"channel_layout", OPT_INPUT | OPT_OUTPUT | OPT_AUDIO | HAS_ARG | OPT_PERFILE | OPT_STRING, "set channel layout", "layout"}, // call set_channel_layout()
                               {"sample_fmt", OPT_INPUT | OPT_OUTPUT | OPT_AUDIO | HAS_ARG | OPT_STRING | OPT_SPEC, "set sample format", "format"},         // sample_fmts

                               {"scodec", OPT_INPUT | OPT_OUTPUT | OPT_SUBTITLE | IS_ALIAS, "codec:s", ""},
                               {"sn", OPT_INPUT | OPT_OUTPUT | OPT_SUBTITLE | OPT_BOOL, "disable subtitle"}, // subtitle_disable

                               {"dcodec", OPT_INPUT | OPT_OUTPUT | OPT_DATA | IS_ALIAS, "codec:d", ""},
                               {"dn", OPT_INPUT | OPT_OUTPUT | OPT_DATA | OPT_BOOL, "disable data"} // data_disable
                           });
   return defs;
}

OptionDefs &add_in_options(OptionDefs &defs) // append option definitions that are unique to input files
{
   // include INPUT/OUTPUT OPTIONS by default
   defs.insert(defs.end(), {{"accurate_seek", OPT_INPUT | OPT_BOOL, "enable/disable accurate seeking with -ss"}, {"discard", OPT_INPUT | HAS_ARG | OPT_STRING | OPT_SPEC, "discard", ""}, {"dump_attachment", OPT_INPUT | HAS_ARG | OPT_STRING | OPT_SPEC, "extract an attachment into a file", "filename"}, {"itsoffset", OPT_INPUT | HAS_ARG | OPT_TIME, "set the input ts offset", "time_off"}, // input_ts_offset
                            {"itsscale", OPT_INPUT | HAS_ARG | OPT_DOUBLE | OPT_SPEC, "set the input ts scale", "scale"},                                                                                                                                                                                                                                                                          // ts_scale
                            {"re", OPT_INPUT | OPT_BOOL, "read input at native frame rate", ""},                                                                                                                                                                                                                                                                                                   // rate_emu
                            {"seek_timestamp", OPT_INPUT | HAS_ARG | OPT_BOOL, "enable/disable seeking by timestamp with -ss"},
                            {"stream_loop", OPT_INPUT | HAS_ARG | OPT_INT, "set number of times input stream shall be looped", "loop count"}, // loop
                            {"thread_queue_size", OPT_INPUT | HAS_ARG | OPT_INT, "set the maximum number of queued packets from the demuxer"},
                            {"autorotate", OPT_INPUT | OPT_VIDEO | HAS_ARG | OPT_BOOL | OPT_SPEC, "automatically insert correct rotate filters"},
                            {"hwaccel", OPT_INPUT | OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_SPEC, "use HW accelerated decoding", "hwaccel name"},                                    // hwaccels
                            {"hwaccel_device", OPT_INPUT | OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_SPEC, "select a device for HW acceleration", "devicename"},                       // hwaccel_devices
                            {"hwaccel_output_format", OPT_INPUT | OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_SPEC, "select output format used with HW accelerated decoding", "format"}, // hwaccel_output_formats
                            {"guess_layout_max", OPT_INPUT | OPT_AUDIO | HAS_ARG | OPT_INT | OPT_SPEC, "set the maximum number of channels to try to guess the channel layout"},
                            {"canvas_size", OPT_INPUT | OPT_SUBTITLE | HAS_ARG | OPT_STRING | OPT_SPEC, "set canvas size (WxH or abbreviation)", "size"},
                            {"fix_sub_duration", OPT_INPUT | OPT_SUBTITLE | OPT_BOOL | OPT_SPEC, "fix subtitles duration"}});
   return defs;
}

OptionDefs &add_filter_options(OptionDefs &defs)
{
   // ADD FILTER OPTIONS
   defs.insert(defs.end(), {
                               {"filter", OPT_OUTPUT | HAS_ARG | OPT_STRING | OPT_SPEC, "set stream filtergraph", "filter_graph"},                                      // filters
                               {"filter_script", OPT_OUTPUT | HAS_ARG | OPT_STRING | OPT_SPEC, "read stream filtergraph description from a file", "filename"},          // filter_scripts
                               {"filter_complex", OPT_OUTPUT | HAS_ARG | OPT_STRING | OPT_SPEC, "set a complex filtergraph", "graph_description"},                      //
                               {"filter_complex_script", OPT_OUTPUT | HAS_ARG | OPT_STRING | OPT_SPEC, "read complex filtergraph description from a file", "filename"}, // filter_complex_scripts
                               {"reinit_filter", OPT_INPUT | HAS_ARG | OPT_INT | OPT_SPEC, "reinit filtergraph on input parameter changes", ""},                        // reinit_filters
                               {"vf", OPT_OUTPUT | IS_ALIAS, "filter:v", ""},                                                                                           // called opt_video_filters()
                               {"af", OPT_OUTPUT | IS_ALIAS, "filter:a", ""}                                                                                            // called opt_audio_filters()
                           });
   return defs;
}

OptionDefs &add_out_options(OptionDefs &defs) // append option definitions that are unique to output files
{
   defs.insert(defs.end(), {
                               {"map", OPT_OUTPUT | HAS_ARG | OPT_PERFILE | OPT_STRING, "set input stream mapping", "[-]input_file_id[:stream_specifier][,sync_file_id[:stream_specifier]]"},                                            // call set_map()
                               {"map_channel", OPT_OUTPUT | HAS_ARG | OPT_PERFILE | OPT_STRING, "map an audio channel from one stream to another", "file.stream.channel[:syncfile.syncstream]"},                                         // call set_map_channel()
                               {"map_chapters", OPT_OUTPUT | HAS_ARG | OPT_INT, "set chapters mapping", "input_file_index"},                                                                                                             // chapters_input_file
                               {"map_metadata", OPT_OUTPUT | HAS_ARG | OPT_STRING | OPT_SPEC, "set metadata information of outfile from infile", "outfile[,metadata]:infile[,metadata]"}                                                 // metadata_map
                               {"pre", OPT_OUTPUT | HAS_ARG | OPT_STRING | OPT_SPEC, "preset name", "preset"},                                                                                                                           // presets
                               {"to", OPT_OUTPUT | HAS_ARG | OPT_TIME, "record or transcode stop time", "time_stop"},                                                                                                                    // stop_time
                               {"fs", OPT_OUTPUT | HAS_ARG | OPT_INT64, "set the limit file size in bytes", "limit_size"},                                                                                                               // limit_filesize
                               {"timestamp", OPT_OUTPUT | HAS_ARG | OPT_PERFILE | OPT_STRING, "set the recording timestamp ('now' to set the current time)", "time"},                                                                    // called opt_recording_timestamp() -> sets metadata
                               {"metadata", OPT_OUTPUT | HAS_ARG | OPT_STRING | OPT_SPEC, "add metadata", "string=string"},                                                                                                              //
                               {"program", OPT_OUTPUT | HAS_ARG | OPT_STRING | OPT_SPEC, "add program with specified streams", "title=string:st=number..."},                                                                             //
                               {"dframes", OPT_OUTPUT | OPT_DATA | IS_ALIAS, "frames:d", ""},                                                                                                                                            //                                                                                    // called opt_data_frames()
                               {"target", OPT_OUTPUT | HAS_ARG | OPT_PERFILE | OPT_STRING, "specify target file type (\"vcd\", \"svcd\", \"dvd\", \"dv\" or \"dv50\" with optional prefixes \"pal-\", \"ntsc-\" or \"film-\")", "type"}, // called opt_target()
                               {"shortest", OPT_OUTPUT | OPT_BOOL | OPT_OUTPUT, "finish encoding within shortest input"},                                                                                                                //
                               {"apad", OPT_OUTPUT | OPT_STRING | HAS_ARG | OPT_SPEC, "audio pad", ""},                                                                                                                                  //
                               {"copyinkf", OPT_OUTPUT | OPT_BOOL | OPT_SPEC, "copy initial non-keyframes"},                                                                                                                             // copy_initial_nonkeyframes
                               {"copypriorss", OPT_OUTPUT | OPT_INT | HAS_ARG | OPT_SPEC, "copy or discard frames before start time"},                                                                                                   // copy_prior_start
                               {"frames", OPT_OUTPUT | OPT_INT64 | HAS_ARG | OPT_SPEC, "set the number of frames to output", "number"},                                                                                                  // max_frames
                               {"q", OPT_OUTPUT | HAS_ARG | OPT_DOUBLE | OPT_SPEC, "use fixed quality scale (VBR)", "q"},                                                                                                                // qscale
                               {"qscale", IS_ALIAS | OPT_VIDEO, "q:v", ""},                                                                                                                                                              // called opt_qscale()
                               {"profile", OPT_OUTPUT | HAS_ARG | OPT_PERFILE | OPT_STRING, "set profile", "profile"},                                                                                                                   // called opt_profile()
                               {"attach", OPT_OUTPUT | HAS_ARG | OPT_PERFILE | OPT_STRING, "add an attachment to the output file", "filename"},                                                                                          // called opt_attach()
                               // {"disposition", OPT_OUTPUT | OPT_STRING | HAS_ARG | OPT_SPEC, "disposition", ""},                                                                                                                         //
                               {"vframes", IS_ALIAS | OPT_VIDEO, "frames:v", ""},                                                                                                                            // called opt_video_frames()
                               {"aspect", OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_SPEC, "set aspect ratio (4:3, 16:9 or 1.3333, 1.7777)", "aspect"},                                             // frame_aspect_ratios
                               {"rc_override", OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_SPEC, "rate control override for specific intervals", "override"},                                        //
                               {"timecode", OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_PERFILE | OPT_STRING, "set initial TimeCode value.", "hh:mm:ss[:;.]ff"},                                                  // called opt_timecode
                               {"vtag", IS_ALIAS | OPT_VIDEO, "tag:v", ""},                                                                                                                                  //
                               {"pass", OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_SPEC | OPT_INT, "select the pass number (1 to 3)", "n"},                                                                      //
                               {"passlogfile", OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_SPEC, "select two pass log file name prefix", "prefix"},                                                  // passlogfiles
                               {"intra_matrix", OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_SPEC, "specify intra matrix coeffs", "matrix"},                                                          // intra_matrices
                               {"inter_matrix", OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_SPEC, "specify inter matrix coeffs", "matrix"},                                                          // inter_matrices
                               {"chroma_intra_matrix", OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_SPEC, "specify intra matrix coeffs", "matrix"},                                                   // chroma_intra_matrices
                               {"force_fps", OPT_OUTPUT | OPT_VIDEO | OPT_BOOL | OPT_SPEC, "force the selected framerate, disable the best supported framerate selection"},                                  //
                               {"streamid", OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_PERFILE | OPT_STRING, "set the value of an outfile streamid", "streamIndex:value"},                                       //called opt_streamid() -> sets streamid_map
                               {"force_key_frames", OPT_OUTPUT | OPT_VIDEO | OPT_STRING | HAS_ARG | OPT_SPEC, "force key frames at specified timestamps", "timestamps"},                                     // forced_key_frames
                               {"ab", OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_PERFILE, "audio bitrate (please use -b:a)", "bitrate"},                                                                         // called opt_bitrate()
                               {"b", OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_PERFILE, "video bitrate (please use -b:v)", "bitrate"},                                                                          // called opt_bitrate()
                               {"aframes", IS_ALIAS, "frames:a", ""},                                                                                                                                        // called opt_audio_frames()
                               {"aq", IS_ALIAS | OPT_AUDIO, "q:a", ""},                                                                                                                                      // called opt_audio_qscale()
                               {"atag", IS_ALIAS | OPT_AUDIO, "tag:a", ""},                                                                                                                                  // called opt_old2new()
                               {"stag", IS_ALIAS | OPT_SUBTITLE, "tag:s", ""},                                                                                                                               // called opt_old2new()
                               {"muxdelay", OPT_OUTPUT | OPT_FLOAT | HAS_ARG | OPT_OUTPUT, "set the maximum demux-decode delay", "seconds"},                                                                 // mux_max_delay
                               {"muxpreload", OPT_OUTPUT | OPT_FLOAT | HAS_ARG | OPT_OUTPUT, "set the initial demux-decode delay", "seconds"},                                                               // mux_preload
                               {"sdp_file", OPT_OUTPUT | HAS_ARG | OPT_STRING, "specify a file in which to print sdp information", "file"},                                                                  // called opt_sdp_file() -> sets sdp_file
                               {"bsf", OPT_OUTPUT | HAS_ARG | OPT_STRING | OPT_SPEC, "A comma-separated list of bitstream filters", "bitstream_filters"},                                                    // bitstream_filters
                               {"apre", OPT_OUTPUT | HAS_ARG | OPT_AUDIO | OPT_PERFILE | OPT_STRING, "set the audio options to the indicated preset", "preset"},                                             // called opt_preset()
                               {"vpre", OPT_OUTPUT | OPT_VIDEO | HAS_ARG | OPT_PERFILE | OPT_STRING, "set the video options to the indicated preset", "preset"},                                             // called opt_preset()
                               {"spre", OPT_OUTPUT | HAS_ARG | OPT_SUBTITLE | OPT_PERFILE | OPT_STRING, "set the subtitle options to the indicated preset", "preset"},                                       // called opt_preset()
                               {"fpre", OPT_OUTPUT | HAS_ARG | OPT_PERFILE | OPT_STRING, "set options from indicated preset file", "filename"},                                                              // called opt_preset()
                               {"max_muxing_queue_size", OPT_OUTPUT | HAS_ARG | OPT_INT | OPT_SPEC, "maximum number of packets that can be buffered while waiting for all streams to initialize", "packets"} //
                           });
   return defs;
}

// Global options
// {"y", OPT_BOOL, {&file_overwrite}, "overwrite output files"},
//     {"n", OPT_BOOL, {&no_file_overwrite}, "never overwrite output files"},
//     {"ignore_unknown", OPT_BOOL, {&ignore_unknown_streams}, "Ignore unknown stream types"},
//     {"copy_unknown", OPT_BOOL | OPT_EXPERT, {&copy_unknown_streams}, "Copy unknown stream types"},
//     {"benchmark", OPT_BOOL | OPT_EXPERT, {&do_benchmark}, "add timings for benchmarking"},
//     {"benchmark_all", OPT_BOOL | OPT_EXPERT, {&do_benchmark_all}, "add timings for each task"},
//     {"progress", HAS_ARG | OPT_EXPERT, {.func_arg = opt_progress}, "write program-readable progress information", "url"},
//     {"stdin", OPT_BOOL | OPT_EXPERT, {&stdin_interaction}, "enable or disable interaction on standard input"},
//     {"timelimit", HAS_ARG | OPT_EXPERT, {.func_arg = opt_timelimit}, "set max runtime in seconds", "limit"},
//     {"dump", OPT_BOOL | OPT_EXPERT, {&do_pkt_dump}, "dump each input packet"},
//     {"hex", OPT_BOOL | OPT_EXPERT, {&do_hex_dump}, "when dumping packets, also dump the payload"},
//     {"vsync", HAS_ARG | OPT_EXPERT, {.func_arg = opt_vsync}, "video sync method", ""},
//     {"frame_drop_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT, {&frame_drop_threshold}, "frame drop threshold", ""},
//     {"async", HAS_ARG | OPT_INT | OPT_EXPERT, {&audio_sync_method}, "audio sync method", ""},
//     {"adrift_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT, {&audio_drift_threshold}, "audio drift threshold", "threshold"},
//     {"copyts", OPT_BOOL | OPT_EXPERT, {&copy_ts}, "copy timestamps"},
//     {"start_at_zero", OPT_BOOL | OPT_EXPERT, {&start_at_zero}, "shift input timestamps to start at 0 when using copyts"},
//     {"copytb", HAS_ARG | OPT_INT | OPT_EXPERT, {&copy_tb}, "copy input stream time base when stream copying", "mode"},
//     {"dts_delta_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT, {&dts_delta_threshold}, "timestamp discontinuity delta threshold", "threshold"},
//     {"dts_error_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT, {&dts_error_threshold}, "timestamp error delta threshold", "threshold"},
//     {"xerror", OPT_BOOL | OPT_EXPERT, {&exit_on_error}, "exit on error", "error"},
//     {"abort_on", HAS_ARG | OPT_EXPERT, {.func_arg = opt_abort_on}, "abort on the specified condition flags", "flags"},
//     {"filter_complex", HAS_ARG | OPT_EXPERT, {.func_arg = opt_filter_complex}, "create a complex filtergraph", "graph_description"},
//     {"lavfi", HAS_ARG | OPT_EXPERT, {.func_arg = opt_filter_complex}, "create a complex filtergraph", "graph_description"},
//     {"filter_complex_script", HAS_ARG | OPT_EXPERT, {.func_arg = opt_filter_complex_script}, "read complex filtergraph description from a file", "filename"},
//     {"stats", OPT_BOOL, {&print_stats}, "print progress report during encoding", ""},
//     {"debug_ts", OPT_BOOL | OPT_EXPERT, {&debug_ts}, "print timestamp debugging info", ""},
//     {"max_error_rate", HAS_ARG | OPT_FLOAT, {&max_error_rate}, "maximum error rate", "ratio of errors (0.0: no errors, 1.0: 100% errors) above which ffmpeg returns an error instead of success."},

//     /* video options */
//     {"bits_per_raw_sample", OPT_VIDEO | OPT_INT | HAS_ARG, {&frame_bits_per_raw_sample}, "set the number of bits per raw sample", "number"},
//     {"intra", OPT_VIDEO | OPT_BOOL | OPT_EXPERT, {&intra_only}, "deprecated use -g 1"},
//     {"sameq", OPT_VIDEO | OPT_EXPERT, {.func_arg = opt_sameq}, "Removed"},
//     {"same_quant", OPT_VIDEO | OPT_EXPERT, {.func_arg = opt_sameq}, "Removed"},
//     {"deinterlace", OPT_VIDEO | OPT_BOOL | OPT_EXPERT, {&do_deinterlace}, "this option is deprecated, use the yadif filter instead"},
//     {"psnr", OPT_VIDEO | OPT_BOOL | OPT_EXPERT, {&do_psnr}, "calculate PSNR of compressed frames"},
//     {"vstats", OPT_VIDEO | OPT_EXPERT, {.func_arg = opt_vstats}, "dump video coding statistics to file"},
//     {"vstats_file", OPT_VIDEO | HAS_ARG | OPT_EXPERT, {.func_arg = opt_vstats_file}, "dump video coding statistics to file", "file"},
//     {"qphist", OPT_VIDEO | OPT_BOOL | OPT_EXPERT, {&qp_hist}, "show QP histogram"},
// #if CONFIG_VDA || CONFIG_VIDEOTOOLBOX
//     {"videotoolbox_pixfmt", HAS_ARG | OPT_STRING | OPT_EXPERT, {&videotoolbox_pixfmt}, ""},
// #endif
//     {"hwaccels", OPT_EXIT, {.func_arg = show_hwaccels}, "show available HW acceleration methods"},
//     {"hwaccel_lax_profile_check", OPT_BOOL | OPT_EXPERT, {&hwaccel_lax_profile_check}, "attempt to decode anyway if HW accelerated decoder's supported profiles do not exactly match the stream"},

//     /* audio options */
//     {"vol", OPT_AUDIO | HAS_ARG | OPT_INT, {&audio_volume}, "change audio volume (256=normal)", "volume"},

//     /* subtitle options */

//     /* grab options */
//     {"vc", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {.func_arg = opt_video_channel}, "deprecated, use -channel", "channel"},
//     {"tvstd", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {.func_arg = opt_video_standard}, "deprecated, use -standard", "standard"},
//     {"isync", OPT_BOOL | OPT_EXPERT, {&input_sync}, "this option is deprecated and does nothing", ""},

//     /* muxer options */
//     {"override_ffserver", OPT_BOOL | OPT_EXPERT | OPT_OUTPUT, {&override_ffserver}, "override the options from ffserver", ""},
//     {"sdp_file", HAS_ARG | OPT_EXPERT | OPT_OUTPUT, {.func_arg = opt_sdp_file}, "specify a file in which to print sdp information", "file"},

// /* data codec support */

// #if CONFIG_VAAPI
//     {"vaapi_device", HAS_ARG | OPT_EXPERT, {.func_arg = opt_vaapi_device}, "set VAAPI hardware device (DRM path or X11 display name)", "device"},
// #endif
