function [fs0,T] = get_framerate(infile)
% GETFRAMERATE   Extract (average) frame rate of the video
%   [Fs0,T] = GETFRAMERATE(INFILE)

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
% rev. 1 : (08-30-2017) modified to use ffmpegfileinfo instead of
%                       depricated ffmpeginfo

ffmpegsetenv();
info = ffmpegfileinfo(infile);
if isempty(info.Video)
   error('Input file does not contain any video stream.');
end

fs0 = info.Video(1).AverageFrameRate;

if nargout>1
   T = info.Duration;
end

end
