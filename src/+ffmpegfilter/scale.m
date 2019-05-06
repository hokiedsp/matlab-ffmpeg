classdef scale < ffmpegfilter.base
   % Scale (resize) the input video, using the libswscale library.
   %
   %   Reference:
   %      https://www.ffmpeg.org/ffmpeg-filters.html#scale-1
   properties (Constant)
      nin = [1 1] % range of number of input ports
      nout = [1 1] % range of number of output ports
   end
   properties
      w % Set the output video width expression. Default value is the input width.
      h % Set the output video height expression. Default value is the input height.
      % If the value is 0, the input width is used for the output.
      % If one of the values is -1, the scale filter will use a value that maintains the aspect ratio of the input image, calculated from the other specified dimension. If both of them are -1, the input size is used
      % If one of the values is -n with n > 1, the scale filter will also use a value that maintains the aspect ratio of the input image, calculated from the other specified dimension. After that it will, however, make sure that the calculated dimension is divisible by n and adjust the value if necessary.
      
      interl % {'on','off','auto'}
      %Set the interlacing mode. It accepts the following values:
      %  'on'   - Force interlaced aware scaling
      %  'off'  - Do not apply interlaced scaling.
      %  'auto' - Select interlaced aware scaling depending on whether the source frames are flagged as interlaced or not.
      %Default value is '0'.
      
      flags  % Set libswscale scaling flags. See (ffmpeg-scaler)the ffmpeg-scaler manual for the complete list of values. If not explicitly specified the filter applies the default flags.
      %    'fast_bilinear' - Select fast bilinear scaling algorithm.
      %    'bilinear' - Select bilinear scaling algorithm.
      %    'bicubic' - Select bicubic scaling algorithm.
      %    'experimental' - Select experimental scaling algorithm.
      %    'neighbor' - Select nearest neighbor rescaling algorithm.
      %    'area' - Select averaging area rescaling algorithm.
      %    'bicublin' - Select bicubic scaling algorithm for the luma component, bilinear for chroma components.
      %    'gauss' - Select Gaussian rescaling algorithm.
      %    'sinc' - Select sinc rescaling algorithm.
      %    'lanczos' - Select lanczos rescaling algorithm.
      %    'spline' - Select natural bicubic spline rescaling algorithm.
      %    'print_info' - Enable printing/debug logging.
      %    'accurate_rnd' - Enable accurate rounding.
      %    'full_chroma_int' - Enable full chroma interpolation.
      %    'full_chroma_inp' - Select full chroma input.
      %    'bitexact' - Enable bitexact output.
      
      s % Set the video size. For the syntax of this option, check the (ffmpeg-utils)"Video size" section in the ffmpeg-utils manual.
      
      in_color_matrix  % Set input YCbCr color space type.
      out_color_matrix % Set output YCbCr color space type.
      % This allows the autodetected value to be overridden as well as allows forcing a specific value used for the output and encoder.
      % If not specified, the color space type depends on the pixel format.
      % Possible values:
      %    'auto' - Choose automatically.
      %    'bt709' - Format conforming to International Telecommunication Union (ITU) Recommendation BT.709.
      %    'fcc' - Set color space conforming to the United States Federal Communications Commission (FCC) Code of Federal Regulations (CFR) Title 47 (2003) 73.682 (a).
      %    'bt601' - Set color space conforming to:
      %              ITU Radiocommunication Sector (ITU-R) Recommendation BT.601
      %              ITU-R Rec. BT.470-6 (1998) Systems B, B1, and G
      %              Society of Motion Picture and Television Engineers (SMPTE) ST 170:2004
      %    'smpte240m' Set color space conforming to SMPTE ST 240:1999.
      
      in_range % Set input YCbCr sample range.
      out_range % Set output YCbCr sample range.
      % This allows the autodetected value to be overridden as well as allows forcing a specific value used for the output and encoder. If not specified, the range depends on the pixel format. Possible values:
      %    'auto' - Choose automatically.
      %    'jpeg/full/pc' - Set full range (0-255 in case of 8-bit luma).
      %    'mpeg/tv' - Set "MPEG" range (16-235 in case of 8-bit luma).
      
      force_original_aspect_ratio % Enable decreasing or increasing output video width or height if necessary to keep the original aspect ratio.
      % Possible values:
      %    'disable' - Scale the video as specified and disable this feature.
      %    'decrease' - The output video dimensions will automatically be decreased if needed.
      %    'increase' - The output video dimensions will automatically be increased if needed.
      
      % The out_w, out_h, x, y parameters are expressions containing the following constants:
      %    x     - The computed value for x. They are evaluated for each new frame.
      %    y     - The computed value for y. They are evaluated for each new frame.
      %    in_w  - The input width
      %    iw
      %    in_h  - The input height
      %    ih
      %    out_w - The output (scaled) width
      %    ow
      %    out_h - The output (scaled) height
      %    oh
      %    a     - Same as iw/ih
      %    sar   - input sample aspect ratio
      %    dar   - input display aspect ratio, it is the same as (iw / ih) * sar
      %    hsub  - horizontal chroma subsample value
      %    vsub  - vertical chroma subsample value
      %    n     - The number of the input frame, starting from 0.
      %    pos   - the position in the file of the input frame, NAN if unknown
      %    t     - The timestamp expressed in seconds. It's NAN if the input timestamp is unknown.
   end
   methods (Access=protected)
      function str = print_filter(obj)
         str = 'scale';
         ch = '=';
         for p = {'w','h','in_color_matrix','out_color_matrix','in_range','out_range','force_original_aspect_ratio'}
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
         
         if ~isempty(obj.interl)
            
            if obj.interl(1)=='a'
               str = sprintf('%s%cinterl=-1',str,ch);
            elseif obj.interl(2)=='n'
               str = sprintf('%s%cinterl=1',str,ch);
            else
               str = sprintf('%s%cinterl=0',str,ch);
            end
         end
         
         if ~isempty(obj.flags)
            str = sprintf('%s%csws_flags',str,ch);
            ch = '=';
            for n = 1:numel(obj.flags)
               str = sprintf('%s%c%s',str,ch,obj.flags{n});
               ch = '+';
            end
            ch = ':';
         end

         if ~isempty(obj.s)
            str = sprintf('%s%cs=',str,ch);
            ch = ':';
            if isnumeric(obj.s)
               str = sprintf('%s%dx%d',str,obj.s(1),obj.s(2));
            else
               str = sprintf('%s%s',obj.s);
            end
         end
         
         if ch=='='
            error('Crop filter must have at least one argument set.');
         end
      end
   end
   
   %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
   
   methods
      function scale_frame(obj,scale)
         %SCALE_FRAMESIZE   Resize framesize maintaining the aspect ratio
         %   SCALE_FRAMESIZE(OBJ,FACTOR) sets FFMPEGFILTER.SCALE OBJ's parameters to
         %   scale the video frame size by FACTOR. If FACTOR is a scalar value, the
         %   frame width and height are scaled UP by the same factor. If FACTOR is a
         %   two-element vector [NUM DEN], the width and height are scaled by the
         %   fractional factor NUM/DEN. If NUM/DEN<1, scales down.
         
         validateattributes(obj,{'ffmpegfilter.scale'},{'scalar'},'FFMPEGFILTER.SCALE.SCALE_FRAME','OBJ');
         if all(numel(scale)~=[1 2])
            error('FACTOR must be a scalar or two-element vector.');
         end
         validateattributes(scale,{'numeric'},{'positive','finite'},'FFMPEGFILTER.SCALE.SCALE_FRAME','FACTOR');
         
         if isscalar(scale)
            obj.w = sprintf('%f*in_w',scale(1));
         else
            obj.w = sprintf('%f/%f*in_w',scale(1),scale(2));
         end
         obj.h = -1;
      end
      
      function resize_frame(obj,newsize)
         %SCALE_FRAMESIZE   Resize framesize maintaining the aspect ratio
         %   SCALE_FRAMESIZE(OBJ,SIZE) sets FFMPEGFILTER.SCALE OBJ's parameters to
         %   resize the video frame size to SIZE=[w h].
         
         validateattributes(obj,{'ffmpegfilter.scale'},{'scalar'},'FFMPEGFILTER.SCALE.SCALE_FRAME','OBJ');
         validateattributes(newsize,{'numeric'},{'numel',2,'positive','integer'},'FFMPEGFILTER.SCALE.RESIZE_FRAME','SIZE');
         
         obj.w = newsize(1);
         obj.h = newsize(2);
      end
      
      function set.w(obj,val)
         % If the value is 0, the input width is used for the output. If
         % one of the values is -1, the scale filter will use a value that
         % maintains the aspect ratio of the input image, calculated from
         % the other specified dimension. If both of them are -1, the input
         % size is used If one of the values is -n with n > 1, the scale
         % filter will also use a value that maintains the aspect ratio of
         % the input image, calculated from the other specified dimension.
         % After that it will, however, make sure that the calculated
         % dimension is divisible by n and adjust the value if necessary.
         if ischar(val)
            validateattributes(val,{'char'},{'row'},mfilename,'w');
         else
            validateattributes(val,{'numeric'},{'scalar','integer'},mfilename,'w');
            if val<0, val = -1; end
         end
         obj.w = val;
      end
      function set.h(obj,val)
         % If the value is 0, the input width is used for the output.
         % If one of the values is -1, the scale filter will use a value that maintains the aspect ratio of the input image, calculated from the other specified dimension. If both of them are -1, the input size is used
         % If one of the values is -n with n > 1, the scale filter will also use a value that maintains the aspect ratio of the input image, calculated from the other specified dimension. After that it will, however, make sure that the calculated dimension is divisible by n and adjust the value if necessary.
         if ischar(val)
            validateattributes(val,{'char'},{'row'},mfilename,'h');
         else
            validateattributes(val,{'numeric'},{'scalar','integer'},mfilename,'h');
            if val<0, val = -1; end
         end
         obj.h = val;
      end
      function set.interl(obj,val) %Set the interlacing mode. It accepts the following values:
         %  1 - Force interlaced aware scaling
         %  0 - Do not apply interlaced scaling.
         % -1 - Select interlaced aware scaling depending on whether the
         %      source frames are flagged as interlaced or not.
         %Default value is '0'.
         
         obj.interl = validatestring(val,{'on','off','auto'});
      end
      
      function set.flags(obj,val)  % Set libswscale scaling flags. See (ffmpeg-scaler)the ffmpeg-scaler manual for the complete list of values. If not explicitly specified the filter applies the default flags.
         %    'fast_bilinear' - Select fast bilinear scaling algorithm.
         %    'bilinear' - Select bilinear scaling algorithm.
         %    'bicubic' - Select bicubic scaling algorithm.
         %    'experimental' - Select experimental scaling algorithm.
         %    'neighbor' - Select nearest neighbor rescaling algorithm.
         %    'area' - Select averaging area rescaling algorithm.
         %    'bicublin' - Select bicubic scaling algorithm for the luma component, bilinear for chroma components.
         %    'gauss' - Select Gaussian rescaling algorithm.
         %    'sinc' - Select sinc rescaling algorithm.
         %    'lanczos' - Select lanczos rescaling algorithm.
         %    'spline' - Select natural bicubic spline rescaling algorithm.
         %    'print_info' - Enable printing/debug logging.
         %    'accurate_rnd' - Enable accurate rounding.
         %    'full_chroma_int' - Enable full chroma interpolation.
         %    'full_chroma_inp' - Select full chroma input.
         %    'bitexact' - Enable bitexact output.
         
         algorithms = {'fast_bilinear','bilinear','bicubic',...
            'experimental','neighbor','area','bicublin','gauss','sinc',...
            'lanczos','spline'};
         others = {'print_info','accurate_rnd','full_chroma_int',...
            'full_chroma_inp','bitexact'};
         if ischar(val)
            val = {val};
         elseif ~(iscellstr(val))
            error('flags must be string or cellstr.')
         end
         
         algset = false;
         for n = 1:numel(val)
            if any(strcmpi(val{n},algorithms))
               if algset
                  error('Cannot set more than one algorithm.');
               else
                  algset = true;
               end
            elseif ~any(strcmpi(val{n},others))
               error('Invalid flag specified');
            end
         end
         
         obj.flag = val;
      end
      
      function set.s(obj,val)
         % Specify the size of the sourced video, it may be a string of the
         % form [width height], or the name of a size abbreviation. Set the
         % video size. For the syntax of this option, check the
         % (ffmpeg-utils)"Video size" section in the ffmpeg-utils manual.
         
         if ischar(val)
            if ~any(strcmpi(val,{'ntsc','pal','qntsc','qpal','sntsc','spal',...
                  'film','ntsc-film','sqcif','qcif','cif','4cif','16cif',...
                  'qqvga','qvga','vga','svga','xga','uxga','qxga','sxga',...
                  'qsxga','hsxga','wvga','wxga','wsxga','wuxga','woxga',...
                  'wqsxga','wquxga','whsxga','whuxga','cga','ega','hd480',...
                  'hd720','hd1080','2k','2kflat','2kscope','4k','4kflat',...
                  '4kscope','nhd','hqvga','wqvga','fwqvga','hvga','qhd'}))
               error('Invalid video size name.');
            end
         else
            validateattributes(val,{'numeric'},{'numel',2,'positive','integer','finite'});
         end
         obj.s = val;
      end
      
      function set.in_color_matrix(obj,val)  % Set input YCbCr color space type.
         obj.in_color_matrix = validatestring(val,{'auto','bt709','fcc','bt601','smpte240m'});
      end
      function set.out_color_matrix(obj,val) % Set output YCbCr color space type.
         obj.out_color_matrix = validatestring(val,{'auto','bt709','fcc','bt601','smpte240m'});
      end
      
      function set.in_range(obj,val) % Set input YCbCr sample range.
         obj.in_range = validatestring(val,{'auto','jpeg/full/pc','mpeg/tv'});
      end
      function set.out_range(obj,val) % Set input YCbCr sample range.
         obj.out_range = validatestring(val,{'auto','jpeg/full/pc','mpeg/tv'});
      end
      
      function set.force_original_aspect_ratio(obj,val) % Enable decreasing or increasing output video width or height if necessary to keep the original aspect ratio.
         % Possible values:
         %    'disable' - Scale the video as specified and disable this feature.
         %    'decrease' - The output video dimensions will automatically be decreased if needed.
         %    'increase' - The output video dimensions will automatically be increased if needed.   end
         obj.force_original_aspect_ratio = validatestring(val,{'disable','decrease','increase'});
      end
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
