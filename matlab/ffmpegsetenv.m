function ffmpegsetenv()
%FFMPEGSETENV   Add FFmpeg path to the system path
%   FFMPEGSETENV() adds the FFmpeg folder to the PATH of the system
%   environment. The modified PATH is only valid during the current MATLAB
%   session.

% Copyright 2017 Takeshi Ikuma
% History:
% rev. - : (8-23-2017) original release
% rev. 1 : (8-30-2017) backward compatibility

narginchk(0,1);

syspath = getenv('PATH');
binpath = ffmpegpath();
binpath(binpath=='"') = []; % remove the double quotes
binpath = fileparts(binpath);
if isempty(regexp(syspath,binpath,'once'))
   if syspath(end)~=pathsep
      syspath(end+1) = pathsep;
   end
   setenv('PATH',[syspath binpath pathsep]);
end
