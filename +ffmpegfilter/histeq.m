classdef histeq < ffmpegfilter.base
   %FFMPEGFILTER.HISTEQ   Class for FFMPEG histeq video filter
   % This filter applies a global color histogram equalization on a per-frame basis.
   %
   % It can be used to correct video that has a compressed range of pixel
   % intensities. The filter redistributes the pixel intensities to equalize
   % their distribution across the intensity range. It may be viewed as an
   % "automatically adjusting contrast filter". This filter is useful only for
   % correcting degraded or poorly captured source video.
   %
   %   Reference:
   %      https://www.ffmpeg.org/ffmpeg-filters.html#histeq
   properties (Constant)
      nin = [1 1] % range of number of input ports
      nout = [1 1] % range of number of output ports
   end
   properties
      
      strength
      %Determine the amount of equalization to be applied. As the strength is
      %reduced, the distribution of pixel intensities more-and-more approaches
      %that of the input frame. The value must be a float number in the range
      %[0,1] and defaults to 0.200.
      
      intensity
      %Set the maximum intensity that can generated and scale the output values
      %appropriately. The strength should be set as desired and then the
      %intensity can be limited if needed to avoid washing-out. The value must be
      %a float number in the range [0,1] and defaults to 0.210.
      
      antibanding
      %Set the antibanding level. If enabled the filter will randomly vary the
      %luminance of output pixels by a small amount to avoid banding of the
      %histogram. Possible values are none, weak or strong. It defaults to none.

   end
   methods (Access=protected)
      function str = print_filter(obj)
         
         if isempty(obj.strength)
            str = 'histeq';
            ch = '=';
         else
            str = sprintf('histeq=strength=%f',obj.strength);
            ch = ':';
         end
            
         if ~isempty(obj.intensity)
            str = sprintf('%s%cintensity=%f',str,ch,obj.intensity);
            ch = ':';
         end
            
         if ~isempty(obj.antibanding)
            str = sprintf('%s%cantibanding=%s',str,ch,obj.antibanding);
         end
      end
   end
   
   methods
      function set.strength(obj,val)
         %Determine the amount of equalization to be applied. As the strength is
         %reduced, the distribution of pixel intensities more-and-more approaches
         %that of the input frame. The value must be a float number in the range
         %[0,1] and defaults to 0.200.
         
         validateattributes(val,{'numeric'},{'scalar','nonegative','<=',1});
         obj.strength = val;
         
      end
      
      function set.intensity(obj,val)
         %Set the maximum intensity that can generated and scale the output values
         %appropriately. The strength should be set as desired and then the
         %intensity can be limited if needed to avoid washing-out. The value must be
         %a float number in the range [0,1] and defaults to 0.210.
         
         validateattributes(val,{'numeric'},{'scalar','nonegative','<=',1});
         obj.intensity = val;

      end
      
      function set.antibanding(obj,val)
         %Set the antibanding level. If enabled the filter will randomly vary the
         %luminance of output pixels by a small amount to avoid banding of the
         %histogram. Possible values are none, weak or strong. It defaults to none.
         
         obj.antibanding = validatestring(val,{'none','weak','strong'});
      end
      
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
