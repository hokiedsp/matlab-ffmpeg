classdef transpose < ffmpegfilter.base
   % Transpose rows with columns in the input video and optionally flip it.
   % Reference:
   %    https://www.ffmpeg.org/ffmpeg-filters.html#transpose
   properties (Constant)
      nin = [1 1] % range of number of input ports
      nout = [1 1] % range of number of output ports
   end
   properties
      dir = '' % {'cclock_flip','clock','cclock','clock_flip'} Specify the transposition direction.
      % 'cclock_flip' - Rotate by 90 degrees counterclockwise and vertically flip (default)
      % 'clock' - Rotate by 90 degrees clockwise
      % 'cclock' - Rotate by 90 degrees counterclockwise
      % 'clock_flip' - Rotate by 90 degrees clockwise and vertically flip
      
      passthrough = '' % {'none','portrait','landscape'} Application condition
      % 'none' - Always apply transposition. 
      % 'portrait' - Preserve portrait geometry (when height >= width). 
      % 'landscape' - Preserve landscape geometry (when width >= height). 

   end
   methods (Access=protected)
      function str = print_filter(~)

         if isempty(obj.dir)
            str = 'dir';
            ch = '=';
         else
            str = sprintf('rotate=a=%s',obj.dir);
            ch = ':';
         end
         
         if ~isempty(obj.passthrough)
            str = sprintf('%s%cow=%s',str,ch,obj.passthrough);
         end
      end
   end
   
   methods
      function set.dir(obj,val)
         % 'cclock_flip' - Rotate by 90 degrees counterclockwise and vertically flip (default)
         % 'clock' - Rotate by 90 degrees clockwise
         % 'cclock' - Rotate by 90 degrees counterclockwise
         % 'clock_flip' - Rotate by 90 degrees clockwise and vertically flip
         obj.dir = validatestring(val,{'cclock_flip','clock','cclock','clock_flip'});
      end
      
      function set.passthrough(obj,val)
         % 'none' - Always apply transposition.
         % 'portrait' - Preserve portrait geometry (when height >= width).
         % 'landscape' - Preserve landscape geometry (when width >= height).
         obj.passthrough = validatestring(val,{'none','portrait','landscape'});
      end
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
