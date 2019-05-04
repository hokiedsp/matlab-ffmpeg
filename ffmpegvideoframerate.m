function fps = ffmpegvideoframerate(infile,stream)
%FFMPEGVIDEOFRAMERATE   Retrieves the video stream frame rate in the media file
%   FFMPEGVIDEOFRAMERATE(FILE) returns the average frame rate of the first video 
%   stream types found in the media file specified by the string FILE.
%
%   See Also: FFMPEGMEDIATYPES

% avg: Average framerate
% r: This is the lowest framerate with which all timestamps can be represented accurately (it is the least common multiple of all framerates in the stream). Note, this value is just a guess! For example, if the time base is 1/90000 and all frames have either approximately 3600 or 1800 timer ticks, then r_frame_rate will be 50/1.

% Copyright 2019 Takeshi Ikuma
% History:
% rev. - : (05-03-2019) original release

% Documentation m-file for ffmpegmediatypes.cpp MEX file
