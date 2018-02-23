classdef ImageFilter < matlab.mixin.SetGet & matlab.mixin.CustomDisplay
   % ImageFILTER An FFmpeg-based image filter graph
   %
   %   OBJ = FFMPEG.IMAGEFILTER(FILTERGRAPH) creates an FFmpeg image
   %   filter, with its filter graph specified by FILTERGRAPH. The inputs
   %   and outputs of the filter graph must be AVMEDIA_TYPE_VIDEO.
   %   Moreover, it must produce all the outputs given one set of inputs
   %   images (frames).
   %
   %   OBJ = FFMPEG.IMAGEFILTER(..., 'P1', V1, 'P2', V2, ...)
   %   constructs an image filter object, assigning values V1, V2, etc.
   %   to the specified properties P1, P2, etc. Note that the property
   %   val pairs can be in any format supported by the SET function, e.g.
   %   parameter-val string pairs, structures, or parameter-val cell
   %   array pairs.
   %
   %   Methods:
   %     run           - Run the filter
   %     isSimple      - Returns true if loaded filter graph is simple
   %
   %   Properties:
   %     FilterGraph   - Implemented filtergraph string
   %     InputNames    - Names of the input nodes
   %     OutputNames   - Names of the output nodes
   %     InputFormat   - PixelFormat of the input image
   %     InputSAR      - SAR of the input image
   %     AutoTranspose - Transpose image during filtering to properly set width and height
   %     OutputFormat  - PixelFormat of the output image
   %   
   %     Tag           - Generic string for the user to set.
   %     UserData      - Generic field for any user-defined data.
   %
   %   Static Methods
   %     getFilters    - List of supported FFmpeg filters.
   %     getFilters    - List of supported FFmpeg image formats.
   %
   %   Example:
   %
   %   See also
   %
   
   properties(GetAccess='public', SetAccess='private', Dependent)
      InputNames    % - Names of the input nodes
      OutputNames   % - Names of the output nodes
   end
   
   properties(GetAccess='public', SetAccess='public', SetObservable)
      FilterGraph   % - Implemented filtergraph string
      InputFormat = 'rgb24'   % - PixelFormat of the input image (if multiple-input with multiple formats, use struct to specify each input's)
      InputSAR = 1            % - SAR of the input image (if multiple-input with multiple formats, use struct to specify each input's)

      AutoTranspose = true    % true to match 'width' & 'height' in FFmpeg to match those in MATLAB. If false, they are swapped but faster.
      OutputFormat = 'auto' % 'default' to use the output format of the filter graph as is, or specify a valid pixel format name

   end
   
   properties(GetAccess='public', SetAccess='public')
      Tag = '';       % Generic string for the user to set.
      UserData        % Generic field for any user-defined data.
   end
   
   properties (SetAccess = private, Hidden = true)
      backend % Handle to the backend C++ class instance
   end
   methods (Static, Access = private, Hidden = true)
      varargout = mexfcn(varargin)
   end
   methods
      function obj = ImageFilter(varargin)
         
         narginchk(1,inf);
         try
            validateattributes(varargin{1},{'char'},{'row'},class(obj),'FILTERGRAPH');
         catch
            validateattributes(varargin{1},{'ffmpegfilter.base'},{},class(obj),'FILTERGRAPH');
            varargin{1} = ffmpegfiltergraph(varargin{1});
         end
         varargin = [{'FilterGraph'} varargin];
         
         % instantiate the MEX backend
         ffmpegsetenv(); % make sure ffmpeg DLLs are in the system path
         ffmpeg.ImageFilter.mexfcn(obj);
         
         % set listener for the InputFormat
         addlistener(obj,'InputFormat','PostSet',@(~,~)ffmpeg.ImageFilter.mexfcn(obj,'notifyInputFormatChange'));
         addlistener(obj,'InputSAR','PostSet',@(~,~)ffmpeg.ImageFilter.mexfcn(obj,'notifyInputSARChange'));
         addlistener(obj,'OutputFormat','PostSet',@(~,~)ffmpeg.ImageFilter.mexfcn(obj,'notifyOutputFormatChange'));
         addlistener(obj,'AutoTranspose','PostSet',@(~,~)ffmpeg.ImageFilter.mexfcn(obj,'notifyAutoTransposeChange'));
         
         % set all options
         if nargin>0
            set(obj,varargin{:});
         end
         
      end
      
      function delete(obj)
         if ~isempty(obj.backend)
            ffmpeg.ImageFilter.mexfcn(obj, 'delete');
         end
      end
      
      varargout = run(obj,varargin)
      
      function tf = isSimple(obj)
         % FFMPEG.IMAGEFILTER.ISSIMPLE   True if simple filter graph
         %   ISSIMPLE(OBJ) returns true if loaded filter graph is simple,
         %   i.e., an one-input one-output graph.
         tf = ffmpeg.ImageFilter.mexfcn(obj, 'isSimple');
      end
   end
   
   %------------------------------------------------------------------
   % Documented methods
   %------------------------------------------------------------------
   methods(Access='public')
      
      %------------------------------------------------------------------
      % Overrides of builtins
      %------------------------------------------------------------------
      function c = horzcat(varargin)
         %HORZCAT Horizontal concatenation of FFMPEG.ImageFilter objects.
         %
         %    See also FFMPEG/ImageFilter/VERTCAT, FFMPEG/ImageFilter/CAT.
         if (nargin == 1)
            c = varargin{1};
         else
            error(message('ffmpeg:ImageFilter:noconcatenation'));
         end
      end
      function c = vertcat(varargin)
         %VERTCAT Vertical concatenation of FFMPEG.ImageFilter objects.
         %
         %    See also FFMPEG/ImageFilter/HORZCAT, FFMPEG/ImageFilter/CAT.
         if (nargin == 1)
            c = varargin{1};
         else
            error(message('ffmpeg:ImageFilter:noconcatenation'));
         end
      end
      function c = cat(varargin)
         %CAT Concatenation of FFMPEG.ImageFilter objects.
         %
         %    See also FFMPEG/ImageFilter/VERTCAT, FFMPEG/ImageFilter/HORZCAT.
         if (nargin == 1)
            c = varargin{1};
         else
            error(message('ffmpeg:ImageFilter:noconcatenation'));
         end
      end
   end
   
   methods(Static)
      function filters = getFilters()
         ffmpegsetenv(); % make sure ffmpeg DLLs are in the system path
         filters = ffmpeg.ImageFilter.mexfcn('getFilters');
         if nargout==0
            display(struct2table(filters));
            clear filters
         end
      end
      function formats = getFormats()
         ffmpegsetenv(); % make sure ffmpeg DLLs are in the system path
         formats = ffmpeg.ImageFilter.mexfcn('getFormats');
         if nargout==0
            display(struct2table(formats));
            clear formats
         end
      end
   end
   
   methods(Static, Hidden)
      %------------------------------------------------------------------
      % Persistence
      %------------------------------------------------------------------
      obj = loadobj(B)
   end
   
   %------------------------------------------------------------------
   % Custom Getters/Setters
   %------------------------------------------------------------------
   methods
      % Properties that are not dependent on underlying object.
      function set.Tag(obj, val)
         validateattributes( val, {'char'}, {}, class(obj), 'Tag');
         obj.Tag = val;
      end
      
      % Properties that are dependent on underlying object.
      function val = get.FilterGraph(obj)
         val = ffmpeg.ImageFilter.mexfcn(obj,'get','FilterGraph');
      end
      
      function set.FilterGraph(obj,val)
         validateattributes(val,{'char'},{'row'},class(obj),'FilterGraph');
         ffmpeg.ImageFilter.mexfcn(obj,'set','FilterGraph',val);
         % this action may change InputFormat & InputSAR values
      end
      
      function val = get.InputNames(obj)
         val = ffmpeg.ImageFilter.mexfcn(obj,'get','InputNames');
      end
      
      function val = get.OutputNames(obj)
         val = ffmpeg.ImageFilter.mexfcn(obj,'get','OutputNames');
      end
      
      function set.InputFormat(obj,val)
         if ischar(val)
            if ~(isrow(val) && ffmpeg.ImageFilter.mexfcn('isSupportedFormat',val))
               error('Unsupported input format specified.');
            end
         elseif isstruct(val)
            if ~(isscalar(val) && isempty(setxor(obj.InputNames,fieldnames(val))) ...
              && all(structfun(@(f)ischar(f) && isrow(f) ...
                  && ffmpeg.ImageFilter.mexfcn('isSupportedFormat',f),val))) %#ok
               error('Unsupported input format specified.')
            end
         else
            error('InputFormat must be a string or a struct with input names as field names and their formats as the values.');
         end
         if ~isequal(obj.InputFormat,val)
           obj.InputFormat = val;
          end
      end
      
      function set.InputSAR(obj,val)
         % val could be a scalar between 0 and 1 
         if isstruct(val)
            if ~(isscalar(val) && isempty(setxor(obj.InputNames,fieldnames(val))) ...
              && all(structfun(@(f)ischar(f) && isrow(f) ...
                  && ffmpeg.ImageFilter.isValidSAR(f),val))) %#ok
               error('Input sample-aspect-ratio (SAR) must be provided for all inputs.');
            end
         else
            ffmpeg.ImageFilter.isValidSAR(val);
         end
         if ~isequal(obj.InputSAR,val)
           obj.InputSAR = val;
          end
      end
      function set.AutoTranspose(obj,val)
        validateattributes(val,{'logical'},{'scalar'});
        if ~isequal(obj.AutoTranspose, val)
          obj.AutoTranspose = val;
        end
      end
      function set.OutputFormat(obj,val)
        try
          val = validatestring(val,{'auto'});
        catch
         if ischar(val)
            if ~(isrow(val) && ffmpeg.ImageFilter.mexfcn('isSupportedFormat',val))
               error('Unsupported output format specified.');
            end
         elseif isstruct(val)
            if ~(isscalar(val) && isempty(setxor(obj.OutputNames,fieldnames(val))) ...
              && all(structfun(@(f)ischar(f) && isrow(f) ...
                  && ffmpeg.ImageFilter.mexfcn('isSupportedFormat',f),val))) %#ok
               error('Unsupported output format specified.')
            end
         else
            error('OutputFormat must be ''auto'' or a string or a struct with input names as field names and their formats as the values.');
         end
        end
        if ~isequal(obj.OutputFormat,val)
          obj.OutputFormat = val;
        end
      end
   end
   
   methods (Access=private, Static)
      function tf = isValidSAR(sar)
         tf = true;
         try % possibly a number between 0 and 1
            validateattributes(sar,{'single','double'},{'scalar','positive','finite'});
         catch % or two-element vector to represent a ratio sar(1)/sar(2)
            try
               validateattributes(sar,{'numeric'},{'numel',2,'positive','integer'});
            catch % or a string expression
               ffmpeg.ImageFilter.mexfcn('validateSARString',sar);
            end
         end
      end
   end
   %------------------------------------------------------------------
   % Overrides for Custom Display
   %------------------------------------------------------------------
   methods (Access='protected')
      function propGroups = getPropertyGroups(obj)
         import matlab.mixin.util.PropertyGroup;
         
         if ~isscalar(obj)
            error('Non-scalar object not supported.');
         end
         
         propGroups(1) = PropertyGroup( {'FilterGraph', 'InputFormat', 'InputSAR','OutputFormat','AutoTranspose'});
         propGroups(2) = PropertyGroup( {'InputNames', 'OutputNames'});
         propGroups(3) = PropertyGroup( {'Tag', 'UserData'});
      end
   end
   
   %------------------------------------------------------------------
   % Overrides for Custom Display when calling get(vidObj)
   %------------------------------------------------------------------
   methods (Hidden)
      function getdisp(obj)
         display(obj);
      end
   end
end
