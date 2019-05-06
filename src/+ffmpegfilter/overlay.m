classdef overlay < ffmpegfilter.base
   % Overlay one video on top of another
   %
   % Reference:
   %    https://www.ffmpeg.org/ffmpeg-filters.html#overlay-1

   % Copyright 2015 Takeshi Ikuma
   % History:
   % rev. - : (04-06-2015) original release
   
   properties (Constant)
      nin = [2 2] % range of number of input ports
      nout = [1 1] % range of number of output ports
   end
   properties
      x 
      % Set the expression for the x coordinate of the overlaid video on
      % the main video. Default value is "0". In case the expression is
      % invalid, it is set to a huge value (meaning that the overlay will
      % not be displayed within the output visible area).
 
      y 
      % Set the expression for the y coordinate of the overlaid video on
      % the main video. Default value is "0". In case the expression is
      % invalid, it is set to a huge value (meaning that the overlay will
      % not be displayed within the output visible area).
 
      eof_action 
      % The action to take when EOF is encountered on the secondary input;
      % it accepts one of the following values:
      %   repeat - Repeat the last frame (the default).
      %   endall - End both streams.
      %   pass   - Pass the main input through.
      
      eval 
      % Set when the expressions for x, and y are evaluated.
      % It accepts the following values:
      %   'init' - only evaluate expressions once during the filter
      %            initialization or when a command is processed
      %   'frame' - evaluate expressions for each incoming frame
      % Default value is 'frame'.
      
      shortest % ['on'|{'off'}]
      % If set to 'on', force the output to terminate when the shortest input
      % terminates. Default value is 'off'.
      
      format % Set the format for the output video.
      %It accepts the following values:
      %   'yuv420' - force YUV420 output
      %   'yuv422' - force YUV422 output
      %   'yuv444' - force YUV444 output
      %   'rgb'    - force RGB output
      %Default value is 'yuv420'.
      
      repeatlast % [{'on'}|'off'] 
      % If set to 'on', force the filter to draw the last overlay frame over
      % the main input until the end of the stream. A value of 'off' disables
      % this behavior. Default value is 'on'.

      % The x, and y expressions can contain the following parameters.
      % 
      % main_w, W
      % main_h, H
      % 
      % The main input width and height.
      % overlay_w, w
      % overlay_h, h
      % 
      % The overlay input width and height.
      % x
      % y
      % 
      % The computed values for x and y. They are evaluated for each new frame.
      % hsub
      % vsub
      % 
      % horizontal and vertical chroma subsample values of the output format. For example for the pixel format "yuv422p" hsub is 2 and vsub is 1.
      % n
      % 
      % the number of input frame, starting from 0
      % pos
      % 
      % the position in the file of the input frame, NAN if unknown
      % t
      % 
      % The timestamp, expressed in seconds. It?fs NAN if the input timestamp is unknown.
   end
   methods (Access=protected)
      function str = print_filter(obj)
         str = 'overlay';
      
         ch = '=';
         for p = {'x','y','eof_action','eval','format'}
            pname = p{1};
            pvalue = obj.(pname);
            
            if ~isempty(pvalue)
               if ischar(pvalue)
                  str = sprintf('%s%c%s=%s',str,ch,pname,pvalue);
               else
                  str = sprintf('%s%c%s=%d',str,ch,pname,pvalue);
               end
               ch = ':';
            end
         end
         
         for p = {'shortest','repeatlast'}
            pname = p{1};
            pvalue = obj.(pname);
            if ~isempty(pvalue)
               str = sprintf('%s%c%s=%d',str,ch,pname,pvalue(2)=='n');
               ch = ':';
            end
         end
      end
   end
   
      %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
      
   methods   
      function set.x(obj,val)
         % Set the expression for the x coordinate of the overlaid video on
         % the main video. Default value is "0". In case the expression is
         % invalid, it is set to a huge value (meaning that the overlay
         % will not be displayed within the output visible area).
         if ~ischar(val)
            validateattributes(val,{'numeric'},{'scalar','integer'});
         end
         obj.x = val;
         
      end
         
      function set.y(obj,val)
         % Set the expression for the y coordinate of the overlaid video on
         % the main video. Default value is "0". In case the expression is
         % invalid, it is set to a huge value (meaning that the overlay
         % will not be displayed within the output visible area).
         if ~ischar(val)
            validateattributes(val,{'numeric'},{'scalar','integer'});
         end
         obj.y = val;
         
      end
      
      function set.eof_action(obj,val)
         % The action to take when EOF is encountered on the secondary
         % input; it accepts one of the following values:
         %   repeat - Repeat the last frame (the default). 
         %   endall - End both streams. 
         %   pass   - Pass the main input through.
         obj.eof_action = validatestring(val,{'repeat','endal','pass'});
      end
         
      function set.eval(obj,val)
         % Set when the expressions for x, and y are evaluated.
         %It accepts the following values:
         %   'init' - only evaluate expressions once during the filter initialization or when a command is processed
         %   'frame' - evaluate expressions for each incoming frame
         %Default value is 'frame'.
         obj.eval = validatestring(val,{'init','frame'});
      end
      
      function set.shortest(obj,val)
         % If set to 'on', force the output to terminate when the shortest
         % input terminates. Default value is 'off'.
         obj.shortest = validatestring(val,{'on','off'});
      end
      
      function set.format(obj,val)
         % Set the format for the output video.
         %It accepts the following values:
         %   'yuv420' - force YUV420 output
         %   'yuv422' - force YUV422 output
         %   'yuv444' - force YUV444 output
         %   'rgb'    - force RGB output
         %Default value is 'yuv420'.
         obj.format = validatestring(val,{'yuv420','yuv422','yuv444','rgb'});
      end
      
      function set.repeatlast(obj,val)
         % If set to 1, force the filter to draw the last overlay frame
         % over the main input until the end of the stream. A value of 0
         % disables this behavior. Default value is 1.
         obj.repeatlast = validatestring(val,{'on','off'});
      end
      
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
