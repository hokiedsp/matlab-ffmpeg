classdef tail < ffmpegfilter.base
   % FFMPEGFILTER.TAIL class defines a null filter to indicate the end of a
   % filtergraph. A filtergraph must have only one FFMPEGFILTER.TAIL
   % object. If filtergraph has more than one output, they are defined as
   % the input ports of the FFMPEGFILTER.TAIL object of a filtergraph.
   
   properties (Constant)
      nin = [1 inf] % range of number of input ports
      nout = [0 0] % range of number of output ports
   end
   methods (Access=protected)
      function str = print_filter(~)
         str = '';
      end
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
