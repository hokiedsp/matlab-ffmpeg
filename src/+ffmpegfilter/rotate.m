classdef rotate < ffmpegfilter.base
   %Rotate video by an arbitrary angle expressed in radians.
   %
   %Reference:
   %   https://www.ffmpeg.org/ffmpeg-filters.html#rotate
   properties (Constant)
      nin = [1 1] % range of number of input ports
      nout = [1 1] % range of number of output ports
   end
   properties
      out_auto = 'auto'
      % ['on','off',{'auto'}] Set 'on' to automatically set width & height
      % to fit the rotated video. If auto (default) values are set only for
      % empty out_w or out_h values
      
      angle = []
      % Set an expression for the angle by which to rotate the input video
      % clockwise, expressed as a number of radians. A negative value will
      % result in a counter-clockwise rotation. By default it is set to
      % "0".
      
      out_w = []
      % Set the output width expression, default value is "iw". This
      % expression is evaluated just once during configuration.
      
      out_h = []
      % Set the output height expression, default value is "ih". This
      % expression is evaluated just once during configuration.
      
      bilinear = ''
      % Enable bilinear interpolation if set to 'on', a value of 'off'
      % disables it. Default value is 'on'.
      
      fillcolor = ''
      % Set the color used to fill the output area not covered by the
      % rotated image. For the general syntax of this option, check the
      % "Color" section in the ffmpeg-utils manual. If the special value
      % "none" is selected then no background is printed (useful for
      % example if the background is never shown). Default value is
      % "black".
      
      % The expressions for the angle and the output size can contain the
      % following constants and functions:
      %
      % n Sequential number of the input frame, starting from 0. It is always NAN
      %   before the first frame is filtered.
      % t Time in seconds of the input frame, it is set to 0 when the filter is
      %   configured. It is always NAN before the first frame is filtered.
      % hsub Horizontal chroma subsample value. For example for the pixel format
      %      "yuv422p" hsub is 2.
      % vsub Vertical chroma subsample values. For example for the pixel format
      %      "yuv422p" vsub is 1.
      % in_w, iw The input video width
      % in_h, ih The input video height
      %
      % out_w, ow The output video width
      % out_h, oh The output video height
      %
      % rotw(a) - The minimal width required for completely containing the input
      %           video rotated by a radians. (only for out_w & out_h)
      % roth(a) - The minimal height required for completely containing the input
      %           video rotated by a radians. (only for out_w & out_h)
   end
   methods (Access=protected)
      function str = print_filter(~)
         
         if isempty(obj.angle)
            str = 'rotate';
            ch = '=';
         else
            if ischar(obj.angle)
               str = sprintf('rotate=a=%s',obj.angle);
            else
               str = sprintf('rotate=a=%f',obj.angle);
            end
            ch = ':';
         end
         
         if ~strcmp(obj.out_auto,'on')
            if ~isempty(obj.out_w)
               if ischar(obj.out_w)
                  str = sprintf('%s%cow=%s',str,ch,obj.out_w);
               else
                  str = sprintf('%s%cow=%d',str,ch,obj.out_w);
               end
               ch = ':';
            end
            
            if ~isempty(obj.out_h)
               if ischar(obj.out_h)
                  str = sprintf('%s%cow=%s',str,ch,obj.out_h);
               else
                  str = sprintf('%s%cow=%d',str,ch,obj.out_h);
               end
               ch = ':';
            end
         elseif ~strcmp(obj.out_auto,'off')
            ison = strcmp(obj.out_auto,'on');
            if ison || isempty(obj.out_w)
               str = sprintf('%s%cow=rotw(a)',str,ch);
               ch = ':';
            end
            if ison || isempty(obj.out_h)
               str = sprintf('%s%coh=roth(a)',str,ch);
               ch = ':';
            end
         end
         
         % set bilinear
         if ~isempty(obj.bilinear)
            str = sprintf('%s%cbilinear=%d',str,ch,strcmp(obj.bilinear','on'));
            ch = ':';
         end
         
         % set color
         if ~isempty(obj.color)
            str = sprintf('%s%ccolor=%s',str,ch,obj.color);
         end
      end
   end
   
   methods
      function set.angle(obj,val)
         % Set an expression for the angle by which to rotate the input
         % video clockwise, expressed as a number of radians. A negative
         % value will result in a counter-clockwise rotation. By default it
         % is set to "0".
         
         if ischar(val) && ~isempty(val)
            obj.angle = val;
         else
            validateattributes(val,{'numeric'},{'scalar','finite'});
            obj.angle = val;
         end
      end

      function set.out_auto(obj,val)
         obj.out_auto = validatestring(val,{'on','off','auto'});
      end
      
      function set.out_w(obj,val) 
         % Set the output width expression, default value is "iw". This
         % expression is evaluated just once during configuration.
         if ischar(val) && ~isempty(val)
            obj.out_w = val;
         else
            validateattributes(val,{'numeric'},{'scalar','positive','integer'});
            obj.out_w = val;
         end
      end
      
      function set.out_h(obj,val)
         % Set the output height expression, default value is "ih". This
         % expression is evaluated just once during configuration.
         if ischar(val) && ~isempty(val)
            obj.out_h = val;
         else
            validateattributes(val,{'numeric'},{'scalar','positive','integer'});
            obj.out_h = val;
         end
      end
      
      function set.bilinear(obj,val)
         % Enable bilinear interpolation if set to 'on', a value of 'off'
         % disables it. Default value is 'on'.
         obj.bilinear = validatestring(val,{'on','off'});
      end
      
      function set.fillcolor(obj,val)
         % Set the color used to fill the output area not covered by the
         % rotated image. For the general syntax of this option, check the
         % "Color" section in the ffmpeg-utils manual. If the special value
         % "none" is selected then no background is printed (useful for
         % example if the background is never shown). Default value is
         % "black".
         obj.color = ffmpegcolor(val);
      end
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
