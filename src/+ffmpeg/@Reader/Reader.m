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
   %     AudioFormat      - Video format as it is represented in MATLAB.
   %
   %   Example:
   %       % Construct a multimedia reader object associated with file
   %       % 'xylophone.mp4'.
   %       vidObj = FFmpeg.VideoReader('xylophone.mp4');
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
   end
   
   properties(GetAccess='public', SetAccess='private', Dependent)
      Duration        % Total length of file in seconds.
   end
   
   properties(GetAccess='public', SetAccess='public')
      Tag = '';       % Generic string for the user to set.
      UserData        % Generic field for any user-defined data.
   end
   
   properties(GetAccess='public', SetAccess='private')
      Streams = 'auto'    % Activated streams (excluding those consumed by filters)
      FrameRate = []      % Frame rate of the video in frames per second.
      Height = []         % Height of the video frame in pixels.
      Width = []          % Width of the video frame in pixels.
      dPixelAspectRatio = []
      VideoFormat = 'auto' % Video format as it is represented in MATLAB.
      VideoFilter = ''     % FFmpeg Video filter chain description
      AudioFilter = ''     % FFmpeg Video filter chain description
      SampleRate = []
      NumberOfAudioChannels = []
      ReadMode = 'components' % 'components'(default if pixel is byte size) |'planes' (default if pixel is sub-byte size)
      % Direction = 'forward'
      % BufferSize = 4  % Underlying frame buffer size
   end
   
   %------------------------------------------------------------------
   % Video properties (in alphabetic order)
   %------------------------------------------------------------------
   properties(GetAccess='public', SetAccess='private', Dependent)
      BitsPerPixel    % Bits per pixel of the video data.
   end
   
   properties(Access='public', Dependent)
      CurrentTime     % Location, in seconds, from the start of the
      % file of the current frame to be read.
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
      function obj = Reader(varargin)
         
         narginchk(1,inf);
         validateattributes(varargin{1},{'char'},{'row'},mfilename,'FILENAME');
         try
            filename = which(varargin{1});
            if isempty(filename)
               if exist(varargin{1},'file')
                  filename = varargin{1};
               else
                  error('bad file name.');
               end
            end
         catch
            error('Could not found the specified file: %s',varargin{1});
         end
         
         if nargin>1
            set(obj,varargin{2:end});
            
            % validate requested video frame dimension change
            moddim = [obj.Width~=0 obj.Height~=0 ~isempty(obj.PixelAspectRatio)]*[1;2;4];
            switch moddim
               case 4 % only PAR changed
                  warning('Only PixelAspectRatio set. Maintains the original height.');
               case 7 % all 3
                  error('Cannot set all 3 of Width, Height, and PixelAspectRatio. Pick 2.');
            end
            
         end

         % instantiate the MEX backend
         obj.backend = ffmpeg.VideoReader.mex_backend(obj,filename);
      end
      
      function delete(obj)
         if ~isempty(obj.backend)
            ffmpeg.VideoReader.mex_backend(obj.backend, 'delete');
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
      varargout = readBuffer(obj)
      eof = hasFrame(obj)
      
      %------------------------------------------------------------------
      % Overrides of builtins
      %------------------------------------------------------------------
      function c = horzcat(varargin)
         %HORZCAT Horizontal concatenation of FFMPEG.VIDEOREADER objects.
         %
         %    See also FFMPEG/VIDEOREADER/VERTCAT, FFMPEG/VIDEOREADER/CAT.
         if (nargin == 1)
            c = varargin{1};
         else
            error(message('ffmpeg:Reader:noconcatenation'));
         end
      end
      function c = vertcat(varargin)
         %VERTCAT Vertical concatenation of FFMPEG.VIDEOREADER objects.
         %
         %    See also FFMPEG/VIDEOREADER/HORZCAT, FFMPEG/VIDEOREADER/CAT.
         if (nargin == 1)
            c = varargin{1};
         else
            error(message('ffmpeg:Reader:noconcatenation'));
         end
      end
      function c = cat(varargin)
         %CAT Concatenation of FFMPEG.VIDEOREADER objects.
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
         
      end
      function set.VideoFormat(obj,value)
         try
            value = validatestring(value,{'rgb24','Grayscale','custom'});
         catch
           value = lower(value);
            validateattributes(value,{'char'},{'row'});
            ffmpeg.VideoReader.mex_backend([], 'static', 'validate_pixfmt',value);   
         end
         obj.VideoFormat = value;
      end
      function set.VideoFilter(obj,value)
         value = validateattributes(value,{'char','row'},mfilename,'VideoFilter');
         obj.VideoFilter = value;
      end
      function set.AudioFilter(obj,value)
         value = validateattributes(value,{'char','row'},mfilename,'AudioFilter');
         obj.AudioFilter = value;
      end
      
%       function set.BufferSize(obj,value)
%          validateattributes(value,{'double'},{'scalar','real','positive','integer'});
%          obj.BufferSize = value;
%       end
%       function set.Direction(obj,value)
%          obj.Direction = validatestring(value,{'forward','backward'},mfilename,'Direction');
%       end

      %%%%%%%%%%%%%%%%%%%%%%%%%%
      
      function value = get.AudioCompression(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'getAudioCompression');
      end
      
      function value = get.NumberOfAudioChannels(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'getNumberOfAudioChannels');
      end
      
      function value = get.VideoCompression(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'getVideoCompression');
      end
      
      function value = get.CurrentTime(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'getCurrentTime');
      end
      
      function set.CurrentTime(obj, value)
         if isempty(obj.backend) % if set during initialization
            obj.backend.CurrentTime = value;
         else
            ffmpeg.VideoReader.mex_backend(obj.backend,'setCurrentTime',value);
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
         
         propGroups(1) = PropertyGroup( {'Name', 'Path', 'Duration', 'CurrentTime', 'Tag', 'UserData'});
         propGroups(2) = PropertyGroup( {'Width', 'Height', 'PixelAspectRatio','FrameRate', 'BitsPerPixel', 'VideoFormat','VideoCompression'});
         propGroups(3) = PropertyGroup( {'BufferSize', 'VideoFilter'});
         
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
         result = ffmpeg.VideoReader.mex_backend(obj.backend,'hasAudio');
      end
      
      function result = hasVideo(obj)
         result = ffmpeg.VideoReader.mex_backend(obj.backend,'hasVideo');
      end
   end
end
