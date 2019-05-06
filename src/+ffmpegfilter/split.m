classdef split < ffmpegfilter.base
   % Split input into several identical outputs
   %
   % Reference:
   %    https://www.ffmpeg.org/ffmpeg-filters.html#split_002c-asplit
   properties (Constant)
      nin = [1 1] % range of number of input ports
      nout = [1 inf] % range of number of output ports
   end
   methods (Access=protected)
      function str = print_filter(obj)
         str = 'split';
      end
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
