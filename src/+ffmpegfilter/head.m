classdef head < ffmpegfilter.base
   % FFMPEGFILTER.HEAD class defines a null filter to indicate the
   % beginning of a filtergraph. A filtergraph must have only one
   % FFMPEGFILTER.HEAD object. If filtergraph has more than one input, they
   % are defined as the output ports of the FFMPEGFILTER.HEAD object of a
   % filtergraph.
   
   properties (Constant)
      nin = [0 0] % range of number of input ports
      nout = [1 inf] % range of number of output ports
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
