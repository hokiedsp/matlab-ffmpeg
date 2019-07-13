function info = audioinfo(filename)
%audioinfo Information about an audio file.
%   INFO = ffmpeg.AUDIOINFO(FILENAME) returns a structure whose fields
%   contain information about an audio file. FILENAME is a character vector
%   or string scalar that specifies the name of the audio file. FILENAME
%   must be in the current directory, in a directory on the MATLAB path, or
%   a full path to a file.
%
%   The set of fields in INFO depends on the individual file and its
%   format.  However, the first nine fields are always the same. These
%   common fields are:
%
%   'Filename'          A character vector or string scalar containing the
%                       name of the file 
%   'StreamId'          Data stream ID selected from the file
%   'CompressionMethod' Method of audio compression of the stream
%   'NumChannels'       Number of audio channels of the stream
%   'ChannelLayout'     Layout of audio channels of the stream
%   'SampleRate'        The sample rate (in Hertz) of the stream
%   'TotalSamples'      Total number of audio samples of the stream
%   'Duration'          Total duration of the stream, in seconds
%   'Title'             character vector representing the value of the 
%                       Title tag present in the file. Value is empty
%                       if tag is not present.
%   'Comment'           character vector representing the value of the
%                       Comment tag present in the file. Value 
%                       is empty if tag is not present.
%   'Artist'            character vector representing the value of the 
%                       Artist or Author tag present in the file. Value
%                       is empty if tag not present.
%
%   Format specific fields areas follows:
%
%   'BitsPerSample'     Number of bits per sample in the audio file.
%                       Only supported for WAVE (.wav) and FLAC (.flac)
%                       files. Valid values are 8, 16, 24, 32 or 64.
%
%   'BitRate'           Number of kilobits per second (kbps) used for
%                       compressed audio files. In general, the larger the
%                       BitRate, the higher the compressed audio quality.
%                       Only supported for MP3 (.mp3) and MPEG-4 Audio
%                       (.m4a, .mp4) files.
%
%   See also ffmpeg.AUDIOREAD, ffmpeg.AUDIOWRITE
