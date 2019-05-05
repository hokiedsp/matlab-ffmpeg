function info = ffmpeginfo(varargin)
%FFMPEGINFO   Retrieves media file information
%   FFMPEGINFO(FILE) without any output argument displays the information
%   of the multimedia file, specified by the string FILE.
%
%   INFO = FFMPEGINFO(FILE) returns INFO struct containing the parsed media
%   information of the multimedia file. 
%
%   INFO = FFMPEGINFO(FILE1, FILE2, ) processes multiple media files at
%   once, returning INFO as a struct array.
%
%   INFO = FFMPEGINFO({FILE1 FILE2 ...}) processes multiple media files at
%   once, returning INFO as a struct array.
%
%   INFO Struct Fields:
%   ===============================================
%      .format       file container format
%      .filename     file name/path
%      .metadata     container meta data
%      .duration     total duration in seconds
%      .duration_ts  total duration in timestamps
%      .start        starting time offset in seconds
%      .start_ts     starting time offset in timestamps
%      .bitrate      total bit rate in bits/second
%      .chapters     chapter markers struct
%      .programs     programs struct
%      .streams      media stream struct
%
%   INFO.CHAPTERS Substruct Fields:
%   ===============================================
%      .start        Starting time in seconds
%      .end          Ending time in seconds
%      .metadata     Chapter meta data
%
%   INFO.PROGRAMS Substruct Fields
%   ===============================================
%      .id           Program ID
%      .name         Program name
%      .metadata     Program meta data
%
%   INFO.STREAMS Substruct Fields
%   ================================================
%      .index        Stream ID
%      .codec_name   Codec name
%      .codec_long_name Codec descriptions
%      .profile
%      .codec_type   Stream type (e.g., 'video', 'audio')
%      .codec_tag_string
%      .codec_tag
%      .width       Frame width (video)
%      .height      Frame height (video)
%      .has_b_frames (video)
%      .sample_aspect_ratio         Stream codec info (struct)
%      .meta         Stream meta data (struct)
%
%   INFO.STREAMS.CODEC Subsubstruct Fields
%   ================================================
%      .name            
%      .desc          	
%      .pix_fmt         Frame pixel format
%      .bpc             Bits per coded sample
%      .size            Frame size [width height]
%      .aspectratios    Aspect Ratios struct of [num den]
%                       .SAR   Sample aspect ratio
%                       .DAR   Display aspect ratio
%      .quality         [max min] qualities
%      .bitrate         Bit rate
%      .fps             Average frame rate
%      .tbr             Estimated video stream time base
%      .tbn             Container time base
%      .tbc             Codec time base
%      .disp            List of dispositions
%
%   (2) Audio Codec
%      .name            Codec name
%      .desc          	Codec descriptions
%      .samplerate      Sampling rate
%      .channels        Channel configuration
%      .sample_fmt      Sample format
%      .bitrate         Bit rate
%      .disp            List of dispositions
%
%   (3) Other
%      .name            Codec name
%      .desc          	Codec descriptions
%      .misc            Other info
%
%   Example:
%      ffmpeginfo('xylophone.mpg') % to simply pipe FFmpeg output
%      info = ffmpeginfo('xylophone.mpg') % get parsed data
%
%   See Also: FFMPEGSETUP, FFMPEGTRANSCODE

% Copyright 2013-2019 Takeshi Ikuma
% History:
% rev. - : (06-19-2013) original release
% rev. 1 : (05-05-2019) MEXified

narginchk(1,inf);

if nargin==1 && iscell(varargin{1})
   infile = varargin{1};
else
   infile = varargin;
end

if ~(iscellstr(infile) && all(cellfun(@(c)size(c,1)==1,infile)))
   error('FILE must be given as a string of characters.');
end

% check to make sure the input files exist
file = cellfun(@(f)which(f),infile,'UniformOutput',false);
I = cellfun(@isempty,file);
if any(I)
   if any(cellfun(@(f)isempty(dir(f)),infile(I)))
      error('At least one of the specified files do not exist.');
   else % if file can be located locally, let it pass ('which' function cannot resolve all the files)
      file(I) = infile(I);
   end
end

if (nargout==0)
   % get FFMPEG executable
   ffmpegexe = ffmpegpath();

   [s,msg] = system(sprintf('%s %s',ffmpegexe,sprintf('-i "%s" ',file{:})));

   if s==0
      error('ffmpeginfo failed to run FFmpeg\n\n%s',msg);
   end

   I = regexp(msg,'Input #','start');
   if isempty(I)
      error('Specified file is not FFmpeg supported media file.');
   end

   % remove the no output warning
   msg = regexprep(msg,'At least one output file must be specified\n$','','once');
   disp(msg(I(1):end));
else
   info = ffmpeginfo_mex(file);
end
