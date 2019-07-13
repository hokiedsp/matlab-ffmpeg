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
%   'CompressionMethod' Method of audio compression in the file 
%   'NumChannels'       Number of audio channels in the file.
%   'SampleRate'        The sample rate (in Hertz) of the data in the file.
%   'TotalSamples'      Total number of audio samples in the file.
%   'Duration'          Total duration of the audio in the file, in
%                       seconds. 
%   'Title'             character vector or string scalar representing the 
%                       value of the Title tag present in the file. Value 
%                       is empty if tag is not present.
%   'Comment'           character vector or string scalar representing the
%                       value of the Comment tag present in the file. Value 
%                       is empty if tag is not present.
%   'Artist'            character vector or string scalar representing the
%                       value of the Artist or Author tag present in the 
%                       file. Value is empty if tag not present.
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
