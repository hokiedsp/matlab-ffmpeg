classdef palettegen < ffmpegfilter.base
   %FFMPEGFILTER.PALETTEGEN   Class for FFMPEG palettegen video filter
   %   Generate one palette for a whole video stream.
   %
   %   Reference:
   %      https://www.ffmpeg.org/ffmpeg-filters.html#palettegen
   properties (Constant)
      nin = [1 1] % range of number of input ports
      nout = [1 1] % range of number of output ports
   end
   properties
      max_colors  % the maximum number of colors to quantize in the palette. 
      % Note: the palette will still contain 256 colors; the unused palette entries will be black.

      reserve_transparent = '' % 'on' to reserve one color for transparency default 'on'
      % Create a palette of 255 colors maximum and reserve the last one for transparency. Reserving the transparency color is useful for GIF optimization. If not set, the maximum of colors in the palette will be 256. You probably want to disable this option for a standalone image. Set by default.

      stats_mode % Statistics mode. 
      %   'full' Compute full frame histograms. or
      %   'diff' Compute histograms only for the part that differs from 
      %          previous frame. This might be relevant to give more 
      %          importance to the moving part of your input if the 
      %          background is static. 
      %   Default value is full. 
   end
   methods (Access=protected)
      function str = print_filter(obj)
         
         str = 'palettegen';
         
         if ~isempty(obj.max_colors)
            str = sprintf('%s=max_colors=%d',str,obj.max_colors);
            ch = ':';
         else
            ch = '=';
         end
         
         if ~isempty(obj.reserve_transparent)
            if strcmp(obj.reserve_transparent,'on')
               str = sprintf('%s%creserve_transparent=1',str,ch);
            else
               str = sprintf('%s%creserve_transparent=0',str,ch);
            end
            ch = ':';
         end

         if ~isempty(obj.stats_mode)
            str = sprintf('%s%cstats_mode=%s',str,ch,obj.stats_mode);
         end
         
      end
   end
   methods
      function set.max_colors(obj,val)
         % The maximum number of colors to quantize in the palette. 1-256
         % Note: the palette will still contain 256 colors; the unused palette entries will be black.
         validateattributes(val,{'numeric'},{'scalar','positive','<=',256,'integer'});
         obj.max_colors = val;
      end
         
      function set.reserve_transparent(obj,val)
         % 'on' to reserve one color for transparency default 'on' Create a palette
         % of 255 colors maximum and reserve the last one for transparency.
         % Reserving the transparency color is useful for GIF optimization. If not
         % set, the maximum of colors in the palette will be 256. You probably want
         % to disable this option for a standalone image. Set by default.
         
         obj.reserve_transparent = validatestring(val,{'on','off'});
      end
      
      function set.stats_mode(obj,val)
         % Statistics mode.
         %   'full' Compute full frame histograms. or
         %   'diff' Compute histograms only for the part that differs from previous 
         %          frame. This might be relevant to give more importance to the
         %          moving part of your input if the background is static.
         %   Default value is full. 

         obj.stats_mode = validatestring(val,{'full','diff'});
      end
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
