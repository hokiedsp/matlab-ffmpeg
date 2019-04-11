classdef crop < ffmpegfilter.base
   %FFMPEGFILTER.CROP   Class for FFMPEG crop video filter
   %   Crop the input video to given dimensions.
   %
   %   Reference:
   %      https://www.ffmpeg.org/ffmpeg-filters.html#crop
   
   properties (Constant)
      nin = [1 1] % range of number of input ports
      nout = [1 1] % range of number of output ports
   end
   properties
      w 
      % out_w The width of the output video. It defaults to iw. This
      % expression is evaluated only once during the filter configuration.
      
      h
      % out_h The height of the output video. It defaults to ih. This
      % expression is evaluated only once during the filter configuration.
      
      x
      % The horizontal position, in the input video, of the left edge of
      % the output video. It defaults to (in_w-out_w)/2. This expression is
      % evaluated per-frame.
      
      y
      % The vertical position, in the input video, of the top edge of the
      % output video. It defaults to (in_h-out_h)/2. This expression is
      % evaluated per-frame.
      
      keep_aspect % {'on','off'}
      % If set to 'on' will force the output display aspect ratio to be the
      % same of the input, by changing the output sample aspect ratio. It
      % defaults to 'off'.
      
      % The out_w, out_h, x, y parameters are expressions containing the following constants:
      %    x     - The computed value for x. They are evaluated for each new frame.
      %    y     - The computed value for y. They are evaluated for each new frame.
      %    in_w  - The input width
      %    iw
      %    in_h  - The input height
      %    ih
      %    out_w - The output (cropped) width
      %    ow
      %    out_h - The output (cropped) height
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
         str = 'crop';
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
         
         if ~isempty(obj.keep_aspect)
            str = sprintf('%s%ckeep_aspect=%d',str,ch,obj.keep_aspect(2)=='n');
         end
         
      end
   end
   methods
      function trim_margin(obj,margins)
         %TRIM_MARGIN   Crops margins off the frame
         %   TRIM_MARGIN(OBJ,MARGINS) where 4-element integer vector [left
         %   top right bottom] Video frame cropping.
         
         validateattributes(obj,{'ffmpegfilter.crop'},{'scalar'},['ffmpeg' mfilename '.trim_margin'],'OBJ');
         validateattributes(margins,{'numeric'},{'numel',4,'>=',0,'integer'},[mfilename '.trim_margin'],'MARGINS');
         
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
      function set.keep_aspect(obj,val)
         obj.keep_aspect = validatestring(val,{'on','off'});
      end
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
