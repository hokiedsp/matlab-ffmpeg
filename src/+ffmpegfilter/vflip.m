classdef vflip < ffmpegfilter.base
   % Flip the input video vertically.
   %
   % Reference:
   %   https://www.ffmpeg.org/ffmpeg-filters.html#vflip
   properties (Constant)
      nin = [1 1] % range of number of input ports
      nout = [1 1] % range of number of output ports
   end
   methods (Access=protected)
      function str = print_filter(obj)
         str = 'vflip';
      end
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
