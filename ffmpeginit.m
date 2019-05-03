function ffmpeginit
%FFMPEGINIT   Initializes FFmpeg Toolbox
%   Run FFMPEGINIT before using FFmpeg Toolbox after each time Matlab is
%   started if it is running in Windows or Mac. It is not needed for Linux.
%   However, the FFmpeg and its shared libraries must be installed on
%   Linux. (Under Windows and Mac, these files are automatically downloaded
%   and placed within the ffmpeg toolbox folder.

% Copyright 2019 Takeshi Ikuma
% History:
% rev. - : (05-03-2019) initial release

if isunix
   return;
end

ffmpegdir = fileparts(which(mfilename));
bindir = fullfile(ffmpegdir,'private');

setenv('PATH',[getenv('PATH') pathsep bindir]);
