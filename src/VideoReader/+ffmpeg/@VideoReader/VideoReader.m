classdef VideoReader < matlab.mixin.SetGet & matlab.mixin.CustomDisplay
   % VIDEOREADER Create a FFmpeg-based multimedia reader object.
   %
   %   OBJ = FFMPEG.VIDEOREADER(FILENAME) constructs a multimedia reader
   %   object, OBJ, that can read in video data from a multimedia file. This
   %   object class is compatible with the built-in VideoReader class, but
   %   with added features based on the FFmpeg library. FILENAME is a string
   %   specifying the name of a multimedia file.  There are no restrictions on
   %   file extensions.  By default, MATLAB looks for the file FILENAME on the
   %   MATLAB path.
   %
   %   OBJ = FFMPEG.VIDEOREADER(FILENAME, 'P1', V1, 'P2', V2, ...) constructs
   %   a multimedia reader object, assigning values V1, V2, etc. to the
   %   specified properties P1, P2, etc. Note that the property value pairs
   %   can be in any format supported by the SET function, e.g.
   %   parameter-value string pairs, structures, or parameter-value cell array
   %   pairs.
   %
   %   Methods:
   %     readFrame         - Read the next available frame from a video file.
   %     hasFrame          - Determine if there is a frame available to read
   %                         from a video file.
   %     getFileFormats    - List of known supported video file formats.
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
   %     Height           - Height of the video frame in pixels.
   %     Width            - Width of the video frame in pixels.
   %     VideoFormat      - Video format as it is represented in MATLAB.
   %     FrameRate        - Frame rate of the video in frames per second.
   %     VideoFilter      - FFmpeg video filter chain description
   %
   %     BitsPerPixel     - Bits per pixel of the video data.
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
      FrameRate = []      % Frame rate of the video in frames per second.
      Height = 0         % Height of the video frame in pixels.
      Width = 0          % Width of the video frame in pixels.
      PixelAspectRatio = []
      VideoFormat = 'rgb24'    % Video format as it is represented in MATLAB.
      VideoFilter = '' % FFmpeg Video filter chain description
      ReadMode = 'components' % 'components'(default if pixel is byte size) |'planes' (default if pixel is sub-byte size)
      BufferSize = 4  % Underlying frame buffer size
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
      NumberOfAudioChannels
      VideoCompression
      % NUMBEROFFRAMES property will be removed in a future release. Use
      % CURRENTTIME property instead.
      NumberOfFrames      % Total number of frames in the video stream.
   end
   
   properties (SetAccess = private, Hidden = true)
      backend % Handle to the backend C++ class instance
   end
   methods (Static, Access = private, Hidden = true)
      varargout = mex_backend(varargin)
   end
   methods
      function obj = VideoReader(varargin)
         
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
         
         ffmpegsetenv(); % make sure ffmpeg DLLs are in the system path

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
            error(message('ffmpeg:VideoReader:noconcatenation'));
         end
      end
      function c = vertcat(varargin)
         %VERTCAT Vertical concatenation of FFMPEG.VIDEOREADER objects.
         %
         %    See also FFMPEG/VIDEOREADER/HORZCAT, FFMPEG/VIDEOREADER/CAT.
         if (nargin == 1)
            c = varargin{1};
         else
            error(message('ffmpeg:VideoReader:noconcatenation'));
         end
      end
      function c = cat(varargin)
         %CAT Concatenation of FFMPEG.VIDEOREADER objects.
         %
         %    See also FFMPEG/VIDEOREADER/VERTCAT, FFMPEG/VIDEOREADER/HORZCAT.
         if (nargin == 1)
            c = varargin{1};
         else
            error(message('ffmpeg:VideoReader:noconcatenation'));
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
      
      % Properties that are dependent on underlying object.
      function value = get.Duration(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'get','Duration');
         
         % Duration property is set to empty if it cannot be determined
         % from the video. Generate a warning to indicate this.
         if isempty(value)
            warnState=warning('off','backtrace');
            c = onCleanup(@()warning(warnState));
            warning(message('ffmpeg:VideoReader:unknownDuration'));
         end
      end
      
      function value = get.BitsPerPixel(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'get','BitsPerPixel');
      end
      function value = get.NumberOfFrames(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'get','NumberOfFrames');
         
         % NumberOfFrames property is set to empty if it cannot be
         % determined by from the video. Generate a warning in this
         % case.
         if isempty(value)
            warnState=warning('off','backtrace');
            c = onCleanup(@()warning(warnState));
            warning(message('ffmpeg:VideoReader:unknownNumFrames'));
         end
      end
      
      function set.FrameRate(obj,value)
         validateattributes(value,{'double'},{'scalar','real','positive','finite'});
         obj.FrameRate = value;
      end
      function set.Height(obj,value)
         % If the width or w value is 0, the input width is used for the output. If
         % the height or h value is 0, the input height is used for the output. 
         % 
         % If one and only one of the values is -n with n >= 1, the scale filter
         % will use a value that maintains the aspect ratio of the input image,
         % calculated from the other specified dimension. After that it will,
         % however, make sure that the calculated dimension is divisible by n and
         % adjust the value if necessary.
         % 
         % If both values are -n with n >= 1, the behavior will be identical to both
         % values being set to 0 as previously detailed. 
         validateattributes(value,{'double'},{'scalar','real','integer'});
         obj.Height = value;
      end
      function set.Width(obj,value)
         validateattributes(value,{'double'},{'scalar','real','integer'});
         obj.Width = value;
      end
      function set.PixelAspectRatio(obj,value)
         validateattributes(value,{'double'},{'vector','numel',2,'positive','finite'});
         obj.PixelAspectRatio = value;
      end
      function set.VideoFormat(obj,value)
         try
            value = validatestring(value,{'grayscale'});
            %value = validatestring(value,{'rgb24','Grayscale'});
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
      function set.BufferSize(obj,value)
         validateattributes(value,{'double'},{'scalar','real','positive','integer'});
         obj.BufferSize = value;
      end

      %%%%%%%%%%%%%%%%%%%%%%%%%%
      
      function value = get.AudioCompression(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'get','AudioCompression');
      end
      
      function value = get.NumberOfAudioChannels(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'get','NumberOfAudioChannels');
      end
      
      function value = get.VideoCompression(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'get','VideoCompression');
      end
      
      function value = get.CurrentTime(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'get','CurrentTime');
      end
      
      function set.CurrentTime(obj, value)
         if isempty(obj.backend) % if set during initialization
            obj.backend.CurrentTime = value;
         else
            ffmpeg.VideoReader.mex_backend(obj.backend,'set','CurrentTime',value);
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
         %             getString( message('ffmpeg:VideoReader:GeneralProperties') ) );
         %
         %          propGroups(2) = PropertyGroup( {'Width', 'Height', 'FrameRate', 'BitsPerPixel', 'VideoFormat'}, ...
         %             getString( message('ffmpeg:VideoReader:VideoProperties') ) );
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
