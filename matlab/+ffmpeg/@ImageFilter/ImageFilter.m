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
   %   value pairs can be in any format supported by the SET function, e.g.
   %   parameter-value string pairs, structures, or parameter-value cell
   %   array pairs.
   %
   %   Methods:
   %     run           - Run the filter
   %     reset         - Resets the FFmpeg object (in case the filter
   %                     graph contains any persisting affects
   %     isSimple      - Returns true if loaded filter graph is simple
   %
   %   Properties:
   %     FilterGraph   - Implemented filtergraph string
   %     InputNames    - Names of the input nodes
   %     OutputNames   - Names of the output nodes
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
   
   properties(Access='public', Dependent)
      FilterGraph   % - Implemented filtergraph string
   end
   
   properties(GetAccess='public', SetAccess='private', Dependent)
      InputNames    % - Names of the input nodes
      OutputNames   % - Names of the output nodes
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
      
      function reset(obj)
      % FFMPEG.IMAGEFILTER.RESET   Reset FFmpeg states
      %   RESET(OBJ) resets internal FFmpeg states by reconstruct the
      %   filtergraph object
      ffmpeg.ImageFilter.mexfcn(obj, 'reset');
      end
      
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
      function set.Tag(obj, value)
         validateattributes( value, {'char'}, {}, class(obj), 'Tag');
         obj.Tag = value;
      end
      
      % Properties that are dependent on underlying object.
      function value = get.FilterGraph(obj)
         value = ffmpeg.ImageFilter.mexfcn(obj,'get','FilterGraph');
      end
      
      function set.FilterGraph(obj,value)
         validateattributes(value,{'char'},{'row'},class(obj),'FilterGraph');
         ffmpeg.ImageFilter.mexfcn(obj,'set','FilterGraph',value);
      end

      function value = get.InputNames(obj)
         value = ffmpeg.ImageFilter.mexfcn(obj,'get','InputNames');
      end

      function value = get.OutputNames(obj)
         value = ffmpeg.ImageFilter.mexfcn(obj,'get','OutputNames');
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
         
         propGroups(1) = PropertyGroup( {'FilterGraph', 'Tag', 'UserData'});
         propGroups(2) = PropertyGroup( {'InputNames', 'OutputNames'});
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
