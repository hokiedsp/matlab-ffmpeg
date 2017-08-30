function ffmpegsetpath()
%FFMPEGSETPATH   Add FFmpeg path to the system path
%   FFMPEGSETPATH() adds the FFmpeg folder to the PATH of the system
%   environment. The modified PATH is only valid during the current MATLAB
%   session.

% Copyright 2017 Takeshi Ikuma
% History:
% rev. - : (8-23-2017) original release

narginchk(0,1);

syspath = getenv('PATH');
binpath = fileparts(ffmpegpath());

if ~contains(syspath,binpath)
   setenv('PATH',[syspath binpath pathsep]);
end
