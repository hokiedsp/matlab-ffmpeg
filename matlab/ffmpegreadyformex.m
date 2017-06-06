function tf = ffmpegreadyformex
%FFMPEGREADYFORMEX   Returns true if FFmpeg shared library files are present

% Copyright 2017 Takeshi Ikuma
% History:
% rev. - : (05-31-2017) original release

narginchk(0,0);
nargoutchk(0,1);

libnames = {'avcodec','avfilter','avformat','avutil','swresample','swscale'};

%if ispc % may be different for different platform
p = fileparts(ffmpegpath);
libtf = true(size(libnames));
for n = 1:numel(libtf)
   libtf(n) = isempty(dir(fullfile(p,[libnames{n},'*'])));
end

libtf = ~any(libtf);

if nargout==0
   if libtf
      disp('All the necessary FFmpeg shared library files found.');
   else
      error('Missing shared library files. ''Shared'' FFmpeg build must be used.');
   end
else
   tf = libtf;
end
