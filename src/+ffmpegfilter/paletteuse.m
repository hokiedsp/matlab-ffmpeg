classdef paletteuse < ffmpegfilter.base
   %FFMPEGFILTER.PALETTEUSE   Class for FFMPEG paletteuse video filter
   %   Use a palette to reduce colors in an input video stream.
   %
   %   Reference:
   %      https://www.ffmpeg.org/ffmpeg-filters.html#paletteuse
   properties (Constant)
      nin = [2 2] % range of number of input ports
      nout = [1 1] % range of number of output ports
   end
   properties
      
      dither %Select dithering mode. Available algorithms are:
      %     'bayer' Ordered 8x8 bayer dithering (deterministic) 
      %     'heckbert' Dithering as defined by Paul Heckbert in 1982 (simple error diffusion). Note: this dithering is sometimes considered "wrong" and is included as a reference. 
      %     'floyd_steinberg' Floyd and Steingberg dithering (error diffusion) 
      %     'sierra2' Frankie Sierra dithering v2 (error diffusion) 
      %     'sierra2_4a' Frankie Sierra dithering v2 "Lite" (error diffusion) 
      % 
      %     Default is sierra2_4a.

      bayer_scale % the scale of the bayer dithering pattern 
      %      (how much the crosshatch pattern is visible). A low value means more visible pattern for less banding, and higher value means less visible pattern at the cost of more banding.
      %      The option must be an integer value in the range [0,5]. Default is 2.

      diff_mode % the zone to process
      %   'off'
      %   'rectangle' Only the changing rectangle will be reprocessed. This is similar to GIF cropping/offsetting compression mechanism. This option can be useful for speed if only a part of the image is changing, and has use cases such as limiting the scope of the error diffusal dither to the rectangle that bounds the moving scene (it leads to more deterministic output if the scene doesn't change much, and as a result less moving noise and better GIF compression). 
      %   Default is none.

   end
   methods (Access=protected)
      function str = print_filter(obj)
         
         str = 'paletteuse';
         
         isbayer = ~isempty(obj.dither);
         if isbayer
            isbayer = strcmp(obj.dither,'bayer');
            str = sprintf('%s=dither=%s',str,obj.dither);
            ch = ':';
         else
            ch = '=';
         end
         
         if isbayer && ~isempty(obj.bayer_scale)
            str = sprintf('%s%cbayer_scale=%d',str,ch,obj.bayer_scale);
            ch = ':';
         end

         if ~(isempty(obj.diff_mode) || strcmp(obj.diff_mode,'off'))
            str = sprintf('%s%cdiff_mode=%s',str,ch,obj.diff_mode);
         end
         
      end
   end
   methods
      function set.dither(obj,val) 
         %Select dithering mode. Available algorithms are:
         %     'bayer'           Ordered 8x8 bayer dithering (deterministic)
         %     'heckbert' -      Dithering as defined by Paul Heckbert in 1982
         %                       (simple error diffusion). Note: this dithering is
         %                       sometimes considered "wrong" and is included as a
         %                       reference.
         %     'floyd_steinberg' Floyd and Steingberg dithering (error diffusion)
         %     'sierra2'         Frankie Sierra dithering v2 (error diffusion)
         %     'sierra2_4a'      [Default] Frankie Sierra dithering v2 "Lite" (error
         %                       diffusion)
         
         obj.dither = validatestring(val,{'bayer','heckbert','floyd_steinberg','sierra2','sierra2_4a'});

      end
      
      function set.bayer_scale(obj,val) 
         %The scale of the bayer dithering pattern
         %      (how much the crosshatch pattern is visible). A low value means more
         %      visible pattern for less banding, and higher value means less
         %      visible pattern at the cost of more banding. The option must be an
         %      integer value in the range [0,5]. Default is 2.

         validateattributes(val,{'numeric'},{'scalar','nonnegative','<=',5,'integer'});
         obj.bayer_scale = val;
      end
      
      function set.diff_mode(obj,val)
         %The zone to process
         %   'none' None zone set
         %   'rectangle' Only the changing rectangle will be reprocessed.
         %   This is similar to GIF cropping/offsetting compression mechanism. This
         %   option can be useful for speed if only a part of the image is changing,
         %   and has use cases such as limiting the scope of the error diffusal
         %   dither to the rectangle that bounds the moving scene (it leads to more
         %   deterministic output if the scene doesn't change much, and as a result
         %   less moving noise and better GIF compression). Default is none.
         
         obj.diff_mode = validatestring(val,{'off','rectangle'});
      end         
   end
end

% Copyright 2015 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
