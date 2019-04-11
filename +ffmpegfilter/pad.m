classdef pad < ffmpegfilter.base
   %FFMPEGFILTER.PAD   A class for FFMPEG pad filter
   %   Pad the input video to given dimensions.
   %
   %   Reference:
   %      https://www.ffmpeg.org/ffmpeg-filters.html#pad
   properties (Constant)
      nin = [1 1] % range of number of input ports
      nout = [1 1] % range of number of output ports
   end
   properties
      w % out_w The width of the output video. It defaults to iw. This expression is evaluated only once during the filter configuration.
      h % out_h The height of the output video. It defaults to ih. This expression is evaluated only once during the filter configuration.
      x % The horizontal position, in the input video, of the left edge of the output video. It defaults to (in_w-out_w)/2. This expression is evaluated per-frame.
      y % The vertical position, in the input video, of the top edge of the output video. It defaults to (in_h-out_h)/2. This expression is evaluated per-frame.
      color % Specify the color of the padded area. For the syntax of this option, check the "Color" section in the ffmpeg-utils manual. The default value of color is "black". 
      
      % The out_w, out_h, x, y parameters are expressions containing the following constants:
      %    x     - The computed value for x. They are evaluated for each new frame.
      %    y     - The computed value for y. They are evaluated for each new frame.
      %    in_w  - The input width
      %    iw
      %    in_h  - The input height
      %    ih
      %    out_w - The output (padded) width
      %    ow
      %    out_h - The output (padded) height
      %    oh
      %    a     - Same as iw/ih
      %    sar   - input sample aspect ratio
      %    dar   - input display aspect ratio, it is the same as (iw / ih) * sar
      %    hsub  - horizontal chroma subsample value
      %    vsub  - vertical chroma subsample value
      %    n     - The number of the input frame, starting from 0.
      %    pos   - the position in the file of the input frame, NAN if unknown
      %    t     - The timestamp expressed in seconds. It’s NAN if the input timestamp is unknown.
   end
   methods (Access=protected)
      function str = print_filter(obj)
         str = 'pad';
         ch = '=';
         for p = {'w','h','x','y'}
            pname = p{1};
            pvalue = obj.(pname);
            
            if ~isempty(pvalue)
               if ischar(pvalue)
                  str = sprintf('%s%c%c=%s',str,ch,pname,pvalue);
               else
                  str = sprintf('%s%c%c=%d',str,ch,pname,pvalue);
               end
               ch = ':';
            end
         end
         
         if ch=='='
            error('Crop filter must have at least one argument set.');
         end
         
         if ~isempty(obj.color)
            str = sprintf('%s%ccolor=%s',str,ch,obj.color);
         end
      end
   end
   
   %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
   
   methods
      function pad_margin(obj,margins)
         %PAD_MARGIN   Pads margins around the frame
         %   PAD_MARGIN(OBJ,MARGINS) where 4-element integer vector [left
         %   top right bottom] Video frame padding.
         
         validateattributes(obj,{'ffmpegfilter.pad'},{'scalar'},[mfilename '.pad_margin'],'OBJ');
         validateattributes(margins,{'numeric'},{'numel',4,'>=',0,'integer'},[mfilename '.pad_margin'],'MARGINS');
         
         obj.w = sprintf('in_w-%d',sum(margins([1 3])));
         obj.h = sprintf('in_h-%d',sum(margins([2 4])));
         obj.x = margins(1);
         obj.y = margins(2);
      end

      %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
      
      function set.w(obj,val)
         if ischar(val)
            validateattributes(val,{'char'},{'row'},mfilename,'w');
         else
            validateattributes(val,{'numeric'},{'scalar','integer','positive'},mfilename,'w');
         end
         obj.w = val;
      end
      function set.h(obj,val)
         if ischar(val)
            validateattributes(val,{'char'},{'row'},mfilename,'h');
         else
            validateattributes(val,{'numeric'},{'scalar','integer','positive'},mfilename,'h');
         end
         obj.h = val;
      end
      function set.x(obj,val)
         if ischar(val)
            validateattributes(val,{'char'},{'row'},mfilename,'x');
         else
            validateattributes(val,{'numeric'},{'scalar','integer'},mfilename,'x');
         end
         obj.x = val;
      end
      function set.y(obj,val)
         if ischar(val)
            validateattributes(val,{'char'},{'row'},mfilename,'y');
         else
            validateattributes(val,{'numeric'},{'scalar','integer'},mfilename,'y');
         end
         obj.y = val;
      end
      function set.color(obj,val)
         obj.color = ffmpegcolor(val);
      end
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
