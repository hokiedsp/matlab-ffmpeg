classdef setsar < ffmpegfilter.base
   % Sets the Sample (aka Pixel) Aspect Ratio.
   %
   %   Reference:
   %      https://ffmpeg.org/ffmpeg-filters.html#setdar_002c-setsar
   
   properties (Constant)
      nin = [1 1] % range of number of input ports
      nout = [1 1] % range of number of output ports
   end
   properties
      ratio  % Set the aspect ratio used by the filter.
             % The parameter can be a floating point number string, an
             % expression, or a string of the form num:den, where num and den
             % are the numerator and denominator of the aspect ratio. If the
             % parameter is not specified, it is assumed the value "0". In case
             % the form "num:den" is used, the : character should be escaped.

      max   % Set the maximum integer value to use for expressing numerator 
            % and denominator when reducing the expressed aspect ratio to a
            % rational. Default value is 100.

   % The parameter ratio is an expression containing the following constants:
   %
   % E, PI, PHI - These are approximated values for the
   %              mathematical constants e (Euler’s number), pi (Greek pi),
   %              and phi (the golden ratio).
   % w, h       - The input width and height.
   % a          - These are the same as w / h.
   % sar        - The input sample aspect ratio.
   % dar        - The input display aspect ratio. It is the same as 
   %              (w / h) * sar.
   % hsub, vsub - Horizontal and vertical chroma subsample values. For 
   %              example, for the pixel format "yuv422p" hsub is 2 and
   %              vsub is 1.

   end
   methods (Access=protected)
      function str = print_filter(obj)
         
         if isempty(obj.ratio)
            error('ratio property must be set to use setsar filter.');
         end
         
         str = 'setsar';
         ch = '=';
         
         for p = {'ratio','max'}
            pname = p{1};
            pvalue = obj.(pname);
            
            if ~isempty(pvalue)
               if ischar(pvalue)
                  str = sprintf('%s%c%s=%s',str,ch,pname,pvalue);
               elseif numel(pvalue)==2
                  str = sprintf('%s%c%s=%d\\:%d',str,ch,pname,pvalue(1),pvalue(2));
               else
                  str = sprintf('%s%c%s=%g',str,ch,pname,pvalue);
               end
               ch = ':';
            end
         end
      end
   end
   
   %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
   
   methods

      function set.ratio(obj,val)
      % Set the aspect ratio used by the filter.
      % The parameter can be a floating point number string, an
      % expression, or a string of the form num:den, where num and den
      % are the numerator and denominator of the aspect ratio. If the
      % parameter is not specified, it is assumed the value "0". In case
      % the form "num:den" is used, the : character should be escaped.

         if ischar(val) % expression
            validateattributes(val,{'char'},{'row'},mfilename,'ratio');
         elseif  numel(val)==2
            validateattributes(val,{'numeric'},{'numel',2,'integer'},mfilename,'ratio');
         else
            validateattributes(val,{'numeric'},{'scalar','nonnegative','finite'},mfilename,'ratio');
         end
         obj.ratio = val;
      end
      
      function set.max(obj,val)
      % Set the maximum integer value to use for expressing numerator 
      % and denominator when reducing the expressed aspect ratio to a
      % rational. Default value is 100.
         if ischar(val)
            validateattributes(val,{'char'},{'row'},mfilename,'max');
         else
            validateattributes(val,{'numeric'},{'scalar','positive','integer'},mfilename,'max');
         end
         obj.max = val;
      end
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (07-22-2015) original release
