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
   %     BitsPerPixel     - Bits per pixel of the video data.
   %     VideoFormat      - Video format as it is represented in MATLAB.
   %     FrameRate        - Frame rate of the video in frames per second.
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
   
   %------------------------------------------------------------------
   % Video properties (in alphabetic order)
   %------------------------------------------------------------------
   properties(GetAccess='public', SetAccess='private', Dependent)
      BitsPerPixel    % Bits per pixel of the video data.
      FrameRate       % Frame rate of the video in frames per second.
      Height          % Height of the video frame in pixels.
      VideoFormat     % Video format as it is represented in MATLAB.
      Width           % Width of the video frame in pixels.
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
         catch
            error('Could not found the specified file: %s',varargin{1});
         end
         
         ffmpegsetpath();
         
         obj.backend = ffmpeg.VideoReader.mex_backend(filename);
         if isempty(obj.backend)
            error('Constructor failed.');
         end
         
         [obj.Path,obj.Name,ext] = fileparts(filename);
         obj.Name = [obj.Name ext];
         
         %             % If no file name provided.
         %             if nargin == 0
         %                 error(message('ffmpeg:VideoReader:noFile'));
         %             end
         %
         %             try
         %                 validateattributes(fileName, {'char'}, {'row', 'vector'}, 'VideoReader');
         %             catch ME
         %                 throwAsCaller(ME);
         %             end
         %
         %             % Initialize the object.
         %             % The duration of the file needs to be determined before the
         %             % CurrentTime can be set.
         %             obj.init(fileName);
         %
         %             % Set properties that user passed in.
         %             if nargin > 1
         %                 set(obj, varargin{:});
         %             end
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
      
      function value = get.FrameRate(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'get','FrameRate');
      end
      
      function value = get.Height(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'get','Height');
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
      
      function value = get.VideoFormat(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'get','VideoFormat');
      end
      
      function value = get.Width(obj)
         value = ffmpeg.VideoReader.mex_backend(obj.backend,'get','Width');
      end
      
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
         ffmpeg.VideoReader.mex_backend(obj.backend,'set','CurrentTime',value);
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
         propGroups(2) = PropertyGroup( {'Width', 'Height', 'FrameRate', 'BitsPerPixel', 'VideoFormat'});
         
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
   
   methods (Static, Access='private', Hidden)
      function fileDesc = translateDescToLocale(fileExtension)
         switch upper(fileExtension)
            case 'M4V'
               fileDesc = getString(message('ffmpeg:VideoReader:formatM4V'));
            case 'MJ2'
               fileDesc = getString(message('ffmpeg:VideoReader:formatMJ2'));
            case 'MOV'
               fileDesc = getString(message('ffmpeg:VideoReader:formatMOV'));
            case 'MP4'
               fileDesc = getString(message('ffmpeg:VideoReader:formatMP4'));
            case 'MPG'
               fileDesc = getString(message('ffmpeg:VideoReader:formatMPG'));
            case 'OGV'
               fileDesc = getString(message('ffmpeg:VideoReader:formatOGV'));
            case 'WMV'
               fileDesc = getString(message('ffmpeg:VideoReader:formatWMV'));
            otherwise
               % This includes formats such as AVI, ASF, ASX.
               fileDesc = getString(message('ffmpeg:VideoReader:formatGeneric', upper(fileExtension)));
         end
      end
      
      function outputFormat = validateOutputFormat(outputFormat, callerFcn)
         validFormats = {'native', 'default'};
         outputFormat = validatestring( outputFormat, validFormats, callerFcn,'outputformat');
      end
      
      function outputFrames = convertToOutputFormat( inputFrames, inputFormat, outputFormat, colormap)
         switch outputFormat
            case 'default'
               outputFrames = VideoReader.convertToDefault(inputFrames, inputFormat, colormap);
            case 'native'
               outputFrames = VideoReader.convertToNative(inputFrames, inputFormat, colormap);
            otherwise
               assert(false, 'Unexpected outputFormat %s', outputFormat);
         end
      end
      
      function outputFrames = convertToDefault(inputFrames, inputFormat, colormap)
         if ~ismember(inputFormat, {'Indexed', 'Grayscale'})
            % No conversion necessary, return the native data
            outputFrames = inputFrames;
            return;
         end
         
         % Return 'Indexed' data as RGB24 when asking for
         % the 'Default' output.  This is done to preserve
         % RGB24 compatibility for customers using versions of
         % VideoReader prior to R2013a.
         outputFrames = zeros(size(inputFrames), 'uint8');
         
         if strcmp(inputFormat, 'Grayscale')
            for ii=1:size(inputFrames, 4)
               % Indexed to Grayscale Image conversion (ind2gray) is part of IPT
               % and not base-MATLAB.
               tempFrame = ind2rgb( inputFrames(:,:,:,ii), colormap);
               outputFrames(:,:,ii) = tempFrame(:, :, 1);
            end
         else
            outputFrames = repmat(outputFrames, [1, 1, 3, 1]);
            for ii=1:size(inputFrames, 4)
               outputFrames(:,:,:,ii) = ind2rgb( inputFrames(:,:,:,ii), colormap);
            end
         end
      end
      
      function outputFrames = convertToNative(inputFrames, inputFormat, colormap)
         if ~ismember(inputFormat, {'Indexed', 'Grayscale'})
            % No conversion necessary, return the native data
            outputFrames = inputFrames;
            return;
         end
         
         % normalize the colormap
         colormap = double(colormap)/255;
         
         numFrames = size(inputFrames, 4);
         outputFrames(1:numFrames) = struct;
         for ii = 1:numFrames
            outputFrames(ii).cdata = inputFrames(:,:,:,ii);
            outputFrames(ii).colormap = colormap;
         end
      end
   end
end
