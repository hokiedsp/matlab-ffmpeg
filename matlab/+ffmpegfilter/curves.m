classdef curves < ffmpegfilter.base
   %FFMPEGFILTER.CURVES   Class for FFMPEG curves video filter
   %   This filter applies color adjustments using curves. This filter is
   %   similar to the Adobe Photoshop and GIMP curves tools. Each component
   %   (red, green and blue) has its values defined by N key points tied
   %   from each other using a smooth curve. The x-axis represents the
   %   pixel values from the input frame, and the y-axis the new pixel
   %   values to be set for the output frame.
   % 
   %   By default, a component curve is defined by the two points (0;0) and
   %   (1;1). This creates a straight line where each original pixel value
   %   is "adjusted" to its own value, which means no change to the image.
   % 
   %   The filter allows you to redefine these two points and add some
   %   more. A new curve (using a natural cubic spline interpolation) will
   %   be define to pass smoothly through all these new coordinates. The
   %   new defined points needs to be strictly increasing over the x-axis,
   %   and their x and y values must be in the [0;1] interval. If the
   %   computed curves happened to go outside the vector spaces, the values
   %   will be clipped accordingly.
   % 
   %   If there is no key point defined in x=0, the filter will
   %   automatically insert a (0;0) point. In the same way, if there is no
   %   key point defined in x=1, the filter will automatically insert a
   %   (1;1) point.
   % 
   %   FFMPEGFILTER.CURVES object has the following properties:
   % 
   %   preset   [{‘none’} ‘color_negative’ ‘cross_process’ ‘darker’
   %             ‘increase_contrast’ ‘lighter’ ‘linear_contrast’
   %             ‘medium_contrast’ ‘negative’ ‘strong_contrast’ ‘vintage’]
   %      Select one of the available color presets. This option can be
   %      used in addition to the red, green, blue properties; in this
   %      case, the later options takes priority on the preset values.
   %
   %   master   two-column matrix; each element between 0 and 1
   %      Set the master key points by its rows. Each row contains [x y]
   %      values of a key point. These key points will define a second pass
   %      mapping. It is sometimes called a "luminance" or "value" mapping.
   %      It can be used with red, green, or blue since it acts like a
   %      post-processing LUT.
   %
   %   red      two-column matrix; each element between 0 and 1
   %      Set the key points for the red component.
   %
   %   green    two-column matrix; each element between 0 and 1
   %      Set the key points for the green component.
   %
   %   blue     two-column matrix; each element between 0 and 1
   %      Set the key points for the blue component.
   %
   %   all      two-column matrix; each element between 0 and 1
   %      Set the key points for all components (not including master). Can
   %      be used in addition to the other key points component options. In
   %      this case, the unset component(s) will fallback on this all
   %      setting.
   %
   %   psfile
   %      Specify a Photoshop curves file (.acv) to import the settings
   %      from.
   % 
   %   Reference:
   %      https://ffmpeg.org/ffmpeg-filters.html#curves
   
   properties (Constant)
      nin = [1 1] % range of number of input ports
      nout = [1 1] % range of number of output ports
   end
   properties
      
   preset = 'none'   % color presets
   %   ‘none’ ‘color_negative’ ‘cross_process’ ‘darker’ ‘increase_contrast’
   %   ‘lighter’ ‘linear_contrast’ ‘medium_contrast’ ‘negative’
   %   ‘strong_contrast’ ‘vintage’
   %
   %      Select one of the available color presets. This option can be
   %      used in addition to the red, green, blue properties; in this
   %      case, the later options takes priority on the preset values.

   master   % two-column matrix; each element between 0 and 1
   %      Set the master key points by its rows. Each row contains [in out]
   %      values of a key point, each between 0 and 1. These key points
   %      will define a second pass mapping. It is sometimes called a
   %      "luminance" or "value" mapping. It can be used with red, green,
   %      or blue since it acts like a post-processing LUT.
   
   red      % two-column matrix; each element between 0 and 1
   %      Set the key points for the red component.
   
   green    % two-column matrix; each element between 0 and 1
   %      Set the key points for the green component.
   
   blue     % two-column matrix; each element between 0 and 1
   %      Set the key points for the blue component.
   
   all      % two-column matrix; each element between 0 and 1
   %      Set the key points for all components (not including master). Can
   %      be used in addition to the other key points component options. In
   %      this case, the unset component(s) will fallback on this all
   %      setting.
   
   psfile
   %      Specify a Photoshop curves file (.acv) to import the settings
   %      from.

   end
   methods (Access=protected)
      function str = print_filter(obj)
         
         if isempty(obj.preset)
            str = 'curves';
            ch = '=';
         else
            str = sprintf('curves=preset=%s',obj.preset);
            ch = ':';
         end

         pnames = {'master','red','green','blue','all'};
         for n = 1:numel(pnames)
            pname = pnames{n};
            if ~isempty(obj.(pname))
               str = sprintf('%s%c%s=''%s''',str,ch,pname,obj.sprintkeypoint(obj.(pname)));
               ch = ':';
            end
         end
         
         if ~isempty(obj.psfile)
            str = sprintf('%s%cpsfile=''%s''',str,ch,obj.psfile);
         end
      end
   end
   
   methods
      function set.preset(obj,val)
         obj.preset = validatestring(val,{'none' 'color_negative' 'cross_process' ...
            'darker' 'increase_contrast' 'lighter' 'linear_contrast' ...
            'medium_contrast' 'negative' 'strong_contrast' 'vintage'},mfilename,'preset');
      end
      
      function set.master(obj,val)
         obj.master = obj.validatekeypoints(val,'master');
      end
      function set.red(obj,val)
         obj.red = obj.validatekeypoints(val,'red');
      end
      function set.green(obj,val)
         obj.green = obj.validatekeypoints(val,'green');
      end
      function set.blue(obj,val)
         obj.blue = obj.validatekeypoints(val,'blue');
      end
      function set.all(obj,val)
         obj.all = obj.validatekeypoints(val,'all');
      end
   end
   methods (Static, Access=private)
      function str = sprintkeypoint(pts)
         Npts = size(pts,1);
         str = sprintf('%f/%f',pts(1,1),pts(1,2));
         for n = 2:Npts
            str = sprintf('%s %f/%f',str,pts(n,1),pts(n,2));
         end
      end
      function val = validatekeypoints(val,pname)
         if ~isempty(val)
            validateattributes(val,{'numeric'},{'2d','ncols',2,'nonempty','nonnegative','<=',1},mfilename,pname);
         end
      end
   end
end

% Copyright 2016 Takeshi Ikuma History: rev. - : (04-19-2016) original
% release
