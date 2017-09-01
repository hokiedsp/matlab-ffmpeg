function [fs0,T] = get_samplerate(infile)
% Returns sample rate of the first audio stream in INFILE

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
% rev. 1 : (08-30-2017) modified to use ffmpegfileinfo instead of
%                       depricated ffmpeginfo

ffmpegsetenv();
info = ffmpegfileinfo(infile);
if isempty(info.Video)
   error('Input file does not contain any audio stream.');
end

fs0 = info.Audio(1).SampleRate;
if nargout>1
   T = info.Duration;
end

end
