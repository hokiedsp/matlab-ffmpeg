classdef Reader < matlab.mixin.SetGet & matlab.mixin.CustomDisplay
   % READER Create a FFmpeg-based multimedia reader object.
   %
   %   OBJ = FFMPEG.READER(FILENAME) constructs a multimedia reader
   %   object, OBJ, that can read video and audio data from a supported 
   %   multimedia file. FILENAME is a string specifying the name of a
   %   multimedia file. There are no restrictions on file extensions.
   %   By default, MATLAB looks for the file FILENAME on the MATLAB path.
   %
   %   OBJ = FFMPEG.READER(FILENAME,STREAMS) specifies which stream to open. 
   %   STREAMS could be a vector of stream ID numbers (integers) or a string
   %   of FFmpeg stream specifier or an cell array, containing either format.
   %
   %   OBJ = FFMPEG.READER(FILENAME, 'P1', V1, 'P2', V2, ...) constructs
   %   a multimedia reader object, assigning values V1, V2, etc. to the
   %   specified properties P1, P2, etc. Note that the property value pairs
   %   can be in any format supported by the SET function, e.g.
   %   parameter-value string pairs, structures, or parameter-value cell array
   %   pairs.
   %
   %   Methods:
   %     readFrame         - Read the next available frame
   %     hasFrame          - Determine if there is a frame available to read
   %     getFileFormats    - List of known supported video file formats
   %
   %   Properties:
   %     Name             - Name of the file to be read.
   %     Path             - Path of the file to be read.
   %     Streams          - Names of active streams.
   %     Duration         - Total length of file in seconds.
   %     CurrentTime      - Location from the start of the file of the current
   %                        frame to be read in seconds.
   %     Tag              - Generic string for the user to set.
   %     UserData         - Generic field for any user-defined data.
   %
   %   (If contains a video stream)
   %     Height           - Height of the video frame in pixels.
   %     Width            - Width of the video frame in pixels.
   %     VideoFormat      - Video format as it is represented in MATLAB.
   %     FrameRate        - Frame rate of the video in frames per second.
   %
   %   (If contains an audio stream)
   %     SampleRate       - Audio stream's sampling rate
   %     NumChannels      - Number of channels
   %     ChannelLayout    - Channel layout
   %     AudioFormat      - Video format as it is represented in MATLAB.
   %
   %   Example:
   %       % Construct a multimedia reader object associated with file
   %       % 'xylophone.mp4'.
   %       vidObj = ffmpeg.Reader('xylophone.mp4');
   %
   %       % Specify that reading should start at 0.5 seconds from the
   %       % beginning.
   %       vidObj.CurrentTime = 0.5;
   %
   %       % Create an axes
   %       currAxes = axes;
   %
   %       % Read video frames until available
   %       while hasFrame(vidObj)
   %           vidFrame = readFrame(vidObj);
   %           image(vidFrame, 'Parent', currAxes);
   %           currAxes.Visible = 'off';
   %           pause(1/vidObj.FrameRate);
   %       end
   %
   %   See also AUDIOVIDEO, VIDEOREADER/READFRAME, VIDEOREADER/HASFRAME, MMFILEINFO.
   %
   
   properties(GetAccess='public', SetAccess='private')
      Name            % Name of the file to be read.
      Path            % Path of the file to be read.
      Streams             % Activated streams (excluding those consumed by filters)
      Duration        % Total length of file in seconds.
   end
   
   properties(Access='public', Dependent)
      CurrentTime     % Location, in seconds, from the start of the
      % file of the current frame to be read.
   end
   
   properties(GetAccess='public', SetAccess='public')
      Tag = '';       % Generic string for the user to set.
      UserData        % Generic field for any user-defined data.
   end
   
   properties(GetAccess='public', SetAccess='private')
      FrameRate = []      % Frame rate of the video in frames per second.
      Height = []         % Height of the video frame in pixels.
      Width = []          % Width of the video frame in pixels.
      PixelAspectRatio = []
      VideoFormat = ''     % Video format as it is represented in MATLAB.
      AudioFormat = ''
      FilterGraph = ''     % FFmpeg Video filter chain description
      SampleRate = []
      NumberOfAudioChannels = []
      ChannelLayout = ''
      % ReadMode = 'components' % 'components'(default if pixel is byte size) |'planes' (default if pixel is sub-byte size)
      % Direction = 'forward'
      % BufferSize = 4  % Underlying frame buffer size
   end
   
   %------------------------------------------------------------------
   % Video properties (in alphabetic order)
   %------------------------------------------------------------------
   properties(GetAccess='public', SetAccess='private', Dependent)
      BitsPerPixel    % Bits per pixel of the video data.
   end
   
   %------------------------------------------------------------------
   % Undocumented properties
   %------------------------------------------------------------------
   properties(GetAccess='public', SetAccess='private', Dependent, Hidden)
      AudioCompression
      VideoCompression
      % NUMBEROFFRAMES property will be removed in a future release. Use
      % CURRENTTIME property instead.
      NumberOfFrames      % Total number of frames in the video stream.
   end
   
   properties (SetAccess = private, Hidden = true)
      backend % Handle to the backend C++ class instance
   end
   methods (Static, Access = private, Hidden = true)
      varargout = mex_backend(varargin)   % mex function
   end
   methods
      function obj = Reader(url,varargin)
         
         % just in case
         ffmpeginit;
         
         % First create the backend object for the given file
         narginchk(1,inf);
         validateattributes(url,{'char'},{'row'},mfilename,'FILENAME');
         url = which(url);
         if isempty(url)
            error('Could not found the specified file: %s',url);
         end
         [obj.Path, obj.Name, ext] = fileparts(url);
         obj.Name = [obj.Name ext];

         % instantiate the MEX backend
         ffmpeg.Reader.mex_backend(obj,url);
         
         % set all the arguments: Streams, VideoFormat, AudioFormat,
         % FilterGraph
         if nargin>1
            set(obj,varargin{:});
         end

         % complete configuration & activate the engine
         % -> this sets all the properties if success
         try
            ffmpeg.Reader.mex_backend(obj,'activate');
         catch ME % if fails, clean up
            ffmpeg.Reader.mex_backend(obj, 'delete');
            obj.backend = [];
            rethrow(ME);
         end
      end
      
      function delete(obj)
         if ~isempty(obj.backend)
            ffmpeg.Reader.mex_backend(obj, 'delete');
         end
      end
   end
   
   %------------------------------------------------------------------
   % Documented methods
   %------------------------------------------------------------------
   methods(Access='public')
      
      %------------------------------------------------------------------
      % Operations
      %------------------------------------------------------------------
      %       inspect(obj)
      
      varargout = readFrame(obj, varargin)
      % varargout = readBuffer(obj)
      eof = hasFrame(obj)
      
      %------------------------------------------------------------------
      % Overrides of builtins
      %------------------------------------------------------------------
      function c = horzcat(varargin)
         %HORZCAT Horizontal concatenation of ffmpeg.Reader objects.
         %
         %    See also FFMPEG/VIDEOREADER/VERTCAT, FFMPEG/VIDEOREADER/CAT.
         if (nargin == 1)
            c = varargin{1};
         else
            error(message('ffmpeg:Reader:noconcatenation'));
         end
      end
      function c = vertcat(varargin)
         %VERTCAT Vertical concatenation of ffmpeg.Reader objects.
         %
         %    See also FFMPEG/VIDEOREADER/HORZCAT, FFMPEG/VIDEOREADER/CAT.
         if (nargin == 1)
            c = varargin{1};
         else
            error(message('ffmpeg:Reader:noconcatenation'));
         end
      end
      function c = cat(varargin)
         %CAT Concatenation of ffmpeg.Reader objects.
         %
         %    See also FFMPEG/VIDEOREADER/VERTCAT, FFMPEG/VIDEOREADER/HORZCAT.
         if (nargin == 1)
            c = varargin{1};
         else
            error(message('ffmpeg:Reader:noconcatenation'));
         end
      end
   end
   methods(Access='public', Hidden)
      varargout = read(obj, varargin)
   end
   
   methods(Static)
      
      %------------------------------------------------------------------
      % Operations
      %------------------------------------------------------------------
      
      formats = getFileFormats()
      formats = getVideoFormats()
      formats = getVideoCompressions()
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
         validateattributes( value, {'char'}, {}, 'set', 'Tag');
         obj.Tag = value;
      end
            
%       function set.FrameRate(obj,value)
%          validateattributes(value,{'double'},{'scalar','real','positive','finite'});
%          obj.FrameRate = value;
%       end
%       function set.Height(obj,value)
%          % If the width or w value is 0, the input width is used for the output. If
%          % the height or h value is 0, the input height is used for the output. 
%          % 
%          % If one and only one of the values is -n with n >= 1, the scale filter
%          % will use a value that maintains the aspect ratio of the input image,
%          % calculated from the other specified dimension. After that it will,
%          % however, make sure that the calculated dimension is divisible by n and
%          % adjust the value if necessary.
%          % 
%          % If both values are -n with n >= 1, the behavior will be identical to both
%          % values being set to 0 as previously detailed. 
%          validateattributes(value,{'double'},{'scalar','real','integer'});
%          obj.Height = value;
%       end
%       function set.Width(obj,value)
%          validateattributes(value,{'double'},{'scalar','real','integer'});
%          obj.Width = value;
%       end
%       function set.PixelAspectRatio(obj,value)
%          validateattributes(value,{'double'},{'vector','numel',2,'positive','finite'});
%          obj.PixelAspectRatio = value;
%       end
      function set.Streams(obj,value)
         % String, numbers, or combination thereof as a cell array. 
         % Maybe empty or 'auto' to pick automatically: one video stream
         % and one audio stream.
         if ~isempty(value)
            if strcmpi(value,'auto')
               value = '';
            else
               Nst = ffmpeg.Reader.mex_backend(obj, 'get_nb_streams');
               if ~iscell(value)
                  value = {value};
               end
               isnum = false(1,numel(value));
               for i = 1:numel(value)
                  if ischar(value{i})
                     validateattributes(value{i},{'char'},{'row','nonempty'});
                  else
                     validateattributes(value{i},{'numeric'},...
                        {'row','nonnegative','<',Nst,'integer','nonempty'});
                     isnum(i) = ~isscalar(value{i});
                  end
               end
               value(isnum) = arrayfun(@(val)num2cell(val),value(isnum),'UniformOutput',false);
            end
         end
         obj.Streams = value;
      end
      function set.VideoFormat(obj,value)
         try
            value = validatestring(value,{'rgb24','Grayscale','native'});
         catch
           value = lower(value);
            validateattributes(value,{'char'},{'row'});
            ffmpeg.Reader.mex_backend('validate_pixfmt',value);   
         end
         obj.VideoFormat = value;
      end
      function set.AudioFormat(obj,value)
         try
            value = validatestring(value,{'native'});
         catch
           value = lower(value);
            validateattributes(value,{'char'},{'row'});
            ffmpeg.Reader.mex_backend('validate_samplefmt',value);   
         end
         obj.AudioFormat = value;
      end
      function set.FilterGraph(obj,value)
         validateattributes(value,{'char'},{'row'});
         obj.FilterGraph = value;
      end
      
%       function set.BufferSize(obj,value)
%          validateattributes(value,{'double'},{'scalar','real','positive','integer'});
%          obj.BufferSize = value;
%       end
%       function set.Direction(obj,value)
%          obj.Direction = validatestring(value,{'forward','backward'},mfilename,'Direction');
%       end

      %%%%%%%%%%%%%%%%%%%%%%%%%%
      
%       function value = get.AudioCompression(obj)
%          value = ffmpeg.Reader.mex_backend(obj,'getAudioCompression');
%       end
%       
      
%       function value = get.VideoCompression(obj)
%          value = ffmpeg.Reader.mex_backend(obj,'getVideoCompression');
%       end
      
      function value = get.CurrentTime(obj)
         value = ffmpeg.Reader.mex_backend(obj,'getCurrentTime');
      end
      
      function set.CurrentTime(obj, value)
         validateattributes(value,{'numeric'},...
            {'scalar','nonnegative','<=',obj.Duration,'nonempty'});
         ffmpeg.Reader.mex_backend(obj,'setCurrentTime',value);
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
         
         propGroups(1) = PropertyGroup( {'Name', 'Path', 'FilterGraph','Streams','Duration', 'CurrentTime'});
         propGroups(2) = PropertyGroup( {'Width', 'Height', 'PixelAspectRatio','FrameRate', 'VideoFormat'});
         propGroups(3) = PropertyGroup( {'NumberOfAudioChannels', 'ChannelLayout', 'SampleRate','AudioFormat'});
         propGroups(4) = PropertyGroup( {'Tag', 'UserData'});
         
         %          propGroups(1) = PropertyGroup( {'Name', 'Path', 'Duration', 'CurrentTime', 'Tag', 'UserData'}, ...
         %             getString( message('ffmpeg:Reader:GeneralProperties') ) );
         %
         %          propGroups(2) = PropertyGroup( {'Width', 'Height', 'FrameRate', 'BitsPerPixel', 'VideoFormat'}, ...
         %             getString( message('ffmpeg:Reader:VideoProperties') ) );
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
   
   %------------------------------------------------------------------
   % Undocumented methods
   %------------------------------------------------------------------
   methods (Access='public', Hidden)
      
      %------------------------------------------------------------------
      % Operations
      %------------------------------------------------------------------
      function result = hasAudio(obj)
         result = ffmpeg.Reader.mex_backend(obj,'hasAudio');
      end
      
      function result = hasVideo(obj)
         result = ffmpeg.Reader.mex_backend(obj,'hasVideo');
      end
   end
end
