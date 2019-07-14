function [y, Fs] = audioread(filename, range, datatype)
%ffmpeg.AUDIOREAD Read audio files using FFmpeg decoder
%   [Y, FS] = ffmpeg.AUDIOREAD(FILENAME) reads an audio file specified by the
%   character vector or string scalar FILENAME, returning the sampled data
%   in Y and the sample rate FS, in Hertz.
%
%   [Y, FS] = ffmpeg.AUDIOREAD(FILENAME, [START END]) returns only samples
%   START through END from each channel in the file. START and END are 
%   specified in sample index.
%
%   [Y, FS] = ffmpeg.AUDIOREAD(FILENAME, DATATYPE) specifies the data type
%   format of Y used to represent samples read from the file. If DATATYPE =
%   'double', Y contains double-precision normalized samples. If DATATYPE =
%   'native', Y contains samples in the native data type found in the file.
%   Interpretation of DATATYPE is case-insensitive and partial matching is
%   supported. If omitted, DATATYPE='double'.
%
%   [Y, FS] = ffmpeg.AUDIOREAD(FILENAME, [START END], DATATYPE);
%
%   Output Data Ranges Y is returned as an m-by-n matrix, where m is the
%   number of audio samples read and n is the number of audio channels in
%   the file.
%
%   If you do not specify DATATYPE, or dataType is 'double', then Y is of
%   type double, and matrix elements are normalized values between -1.0 and
%   1.0.
%
%   Call audioinfo to learn the BitsPerSample of the file.
%
%   Note that where Y is single or double and the BitsPerSample is 32 or
%   64, values in Y might exceed +1.0 or -1.0.
%
%   See also ffmpeg.AUDIOINFO, ffmpeg.AUDIOWRITE
