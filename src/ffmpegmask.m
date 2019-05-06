function ffmpegmask(varargin)
%FFMPEGMASK   Mask video file using FFmpeg
%   FFMPEGMASK(INFILE,MASK,OUTFILE) transcodes the input file specified by
%   the string INFILE using the H.264 video and AAC audio formats. The
%   transcoded data and outputs to the file specified by the string
%   OUTFILE. INFILE must be a FFmpeg supported multimedia file extension
%   (e.g., AVI, MP4, MP3, etc.) while the extension of OUTFILE is expected
%   to be MP4 (although it may output in other formats as well).
%
%   FFMPEGMASK(INFILE,MASK,OUTFILE,'OptionName1',OptionValue1,'OptionName2',OptionValue2,...)
%   may be used to customize the FFmpeg configuration:
%
%      Name    Description
%      ====================================================================
%      BackgroundColor  RGB tuple
%      AutoCrop         [{'on'},'off']
%      Range            Scalar or 2-element vector.
%                       Specifies the segment of INFILE to be transcoded.
%                       If scalar, Range defines the total duration to be
%                       transcoded. If vector, it specifies the starting
%                       and ending times. Range is specified with the input
%                       frame rate or with the OutputFrameRate option if it
%                       given along with FastSearch = 'off'.
%                       
%      Units            [{'seconds'}|'frames'|'samples']
%                       Specifies the units of Range option
%      FastSearch       [{'off'},'on']
%      InputVideoCodec  valid FFMPEG codec name (not validated)
%      InputFrameRate   Positive scalar
%                       Input video frame rate in frames/second. Altering
%                       the input frame rate effectively slows down or
%                       speeds up the video. This option is only valid for
%                       raw video format. Note that when both
%                       InputFrameRate and Range (with Units='seconds') are
%                       specified, Range is defined in the original frame
%                       rate.
%      InputPixelFormat One of format string returned by FFMPEGPIXFMTS
%                       Pixel format.
%      InputFrameSize   Used only if the media file does not store the
%                       frame size. 2 element [w h].
%      InputAudioCodec  One of valid codec string (not validated)
%                       Input audio codec. If 'none', audio data would not be
%                       transcoded.
%      InputSampleRate  Positive scalar
%                       Input audio sampling rate in samples/second.
%                       Only specify if needed to be changed.
%      AudioCodec       [none|{copy}|wav|mp3|aac]
%                       Audio codec. If 'none', audio data would not be
%                       transcoded.
%      AudioSampleRate  Positive scalar
%                       Output audio sampling rate in samples/second.
%                       Only specify if needed to be changed.
%      Mp3Quality       Integer scalar between 0 and 9 {[]}
%                       MP3 encoder quality setting. Lower the higher
%                       quality. Empty uses the FFmpeg default.
%      AacBitRate       Integer scalar.
%                       AAC encoder's target bit rate in b/s. Suggested to
%                       use 64000 b/s per channel.
%      VideoCodec       [none|copy|raw|mpeg4|{x264}|gif]
%                       Video codec. If 'none', video data would not be
%                       transcoded.
%      OutputFrameRate  Positive scalar
%                       Output video frame rate in frames/second.
%      PixelFormat      One of format string returned by FFMPEGPIXFMTS
%                       Pixel format. Default to 'yuv420p' for Apple
%                       QuickTime compatibility if VideoCodec = 'mpeg4' or
%                       'x264' or to 'bgr24' if VideoCodec = 'raw'.
%      x264Preset       [ultrafast|superfast|veryfast|faster|fast|medium|slow|slower|veryslow|placebo]
%                       x264 video codec options to trade off compression
%                       efficiency against encoding speed.
%      x264Tune         film|animation|grain|stillimage|psnr|ssim|fastdecode|zerolatency
%                       x264 video codec options to further optimize for
%                       input content.
%      x264Crf          Integer scaler between 1 and 51 {18}
%                       x264 video codec constant rate factor. Lower the
%                       higher quality, and 18 is considered perceptually
%                       indistinguishable to lossless. Change by ±6 roughly
%                       doubles/halves the file size.
%      Mpeg4Quality     Integer scalar between 1 and 31 {1}
%                       Mpeg4 video codec quality scale. Lower the higher
%                       quality
%      GifLoop          ['off'|{'indefinite'}|positive integer]
%                       Number of times to loop
%      GifFinalDelay    [{'same'}|nonnegative value]
%                       Force the delay (expressed in seconds) after the
%                       last frame. Each frame ends with a delay until the
%                       next frame. If 'same', FFmpeg re-uses the previous
%                       delay. In case of a loop, you might want to
%                       customize this value to mark a pause for instance.
%      GifPaletteStats  [{'full'}|'diff']
%                       Palette is generated based on pixel color
%                       statistics from ('full') every pixels evenly or
%                       ('diff') weighs more on the pixels where changes
%                       occur. Use 'diff' if animation is overlayed on a
%                       still image.
%      GifDither        ['bayer'|'heckbert'|'floyd_steinberg'|'sierra2'|{'sierra2_4a'}]
%                       Dithering algorithm
%      GifDitherBayerScale [0-5] 
%                       Reduce or increase crosshatch patter when Bayer
%                       dithering algorithm is used.
%      GifDitherZone    [{'off'},'rectangle']
%                       Using 'rectangle' option limits re-dithering on a
%                       rectangle section of a frame where motion occurs.
%      ProgressFcn      ['none'|{'default')|function handle]
%                       Callback function to display transcoding progress.
%                       For a custom callback, provide a function handle
%                       with form: progress_fcn(progfile,Nframes), where
%                       'progfile' is the location of the FFmpeg generated
%                       text file containing the transcoding progress and
%                       Nframes is the expected number of video frames in
%                       the output. Note that FFmpeg appends the new
%                       updates to 'progfile'. If set 'default', the
%                       transcoding progress is shown with a waitbar if
%                       video transcoding and no action for audio
%                       transcoding.
%
%   References:
%      FFmpeg Home
%         http://ffmpeg.org
%      FFmpeg Documentation
%         http://ffmpeg.org/ffmpeg.html
%      FFmpeg Wiki Home
%         http://ffmpeg.org/trac/ffmpeg/wiki
%      Encoding VBR (Variable Bit Rate) mp3 audio
%         http://ffmpeg.org/trac/ffmpeg/wiki/Encoding%20VBR%20%28Variable%20Bit%20Rate%29%20mp3%20audio\
%      FFmpeg and AAC Encoding Guide
%         http://ffmpeg.org/trac/ffmpeg/wiki/AACEncodingGuide
%      FFmpeg and x264 Encoding Guide
%         http://ffmpeg.org/trac/ffmpeg/wiki/x264EncodingGuide
%      Xvid/Divx Encoding Guide
%         http://ffmpeg.org/trac/ffmpeg/wiki/How%20to%20encode%20Xvid%20/%20DivX%20video%20with%20ffmpeg
%      MeWiki X264 Settings
%         http://mewiki.project357.com/wiki/X264_Settings
%
%   See Also: FFMPEGSETUP, FFMPEGIMAGE2VIDEO, FFMPEGEXTRACT

% Copyright 2019 Takeshi Ikuma
% History:
% rev. - : (03-14-2019) original release

narginchk(3,inf);

[inopts,outopts,glopts,unkopts,clfcns] = ffmpegtranscode(varargin{[1 3]},true,varargin{4:end});

p = inputParser;
p.addParameter('BackgroundColor',[0 0 0]);
p.addParameter('AutoCrop','off',@ischar);
p.parse(unkopts);

[infile,mask,outfile] = deal(varargin{1:3});
crop = strcmp(validatestring(p.Results.AutoCrop,{'on','off'}),'on');
try
   bgcolor = ffmpegcolor(p.Results.BackgroundColor);
catch
   bgcolor = validatestring(p.Results.BackgroundColor,{'auto'});
end
[maskfile,info,delfcn] = validatemask(mask,infile,crop,bgcolor);

% create complex filter
glopts.filter_complex = create_filter(info);

% only apply input option to the video
inopts = {inopts,struct('loop',true)};

% configure and start progress display (if enabled)
% [glopts,progcleanupfcn] = config_progress(opts.ProgressFcn,infile,opts.Range,fs,mfilename,glopts);

% run FFmpeg
try
   [~] = ffmpegexecargs({infile,maskfile},outfile,inopts,outopts,glopts);
catch ME
%    progcleanupfcn();
   ME.rethrow;
end
% progcleanupfcn();
delfcn();
cellfun(@(fcn)fcn(),clfcns,'UniformOutput',false);

% clean up the temporarily added filters
clfcns{1}();
clfcns{2}();

end

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

function tf = isprogressfcn(val)
tf = isa(val,'function_handle') || any(strcmpi(val,{'default','none'}));
end

function [maskfile,info,delfcn] = validatemask(mask,infile,crop,bgcolor)
   delfcn = {};
   if ischar(mask) % mask image given
      maskfile = which(mask);
      if isempty(maskfile)
         maskfile = which(fullfile(pwd,mask));
         if isempty(maskfile)
            if exist(mask,'file')
               maskfile = mask;
            else
               error('Invalid mask file path given');
            end
         end
      end
      [mask,map] = imread(maskfile);
      if (isempty(map) && any(any(diff(mask,3)))) || (~isempty(map) && any(diff(map,2)))
         warning('Non-grayscale mask given. Automatically converting it to grayscale.');
         if isempty(map)
            mask = rgb2gray(mask);
         end
         maskfile = [tempname '.bmp'];
         delfcn = @()delete(maskfile);
      end
      if ~isempty(map)
         if islogical(mask)
            mask = uint8(mask);
         end
         mask = ind2gray(mask,map);
      end
   else
      if islogical(mask) % mask matrix
         validateattributes(mask,{'logical'},{'2d','nonempty'})
      elseif isinteger(mask) % mask matrix
         validateattributes(mask,{'numeric'},{'2d','nonempty','nonnegative','<',256})
      else
         validateattributes(mask,{'numeric'},{'2d','nonempty','nonnegative','<=',1})
      end
      maskfile = [tempname '.bmp'];
      delfcn = @()delete(maskfile);
   end
   
   % open input video file for validation
   vr = VideoReader(infile);

   % if background color is to be auto-detected
   if strcmp(bgcolor,'auto')
      % sets to 95% brightness based on 3 frames
      Ta = vr.Duration*[0.01 0.5 0.99];
      vr.CurrentTime = Ta(1);
      frm = rgb2gray(vr.readFrame);
      tf = mask>0;
      F = zeros(sum(tf(:)),3,'like',frm);
      F(:,1) = frm(tf);
      vr.CurrentTime = Ta(2);
      frm = rgb2gray(vr.readFrame);
      F(:,2) = frm(tf);
      vr.CurrentTime = Ta(3);
      frm = rgb2gray(vr.readFrame);
      F(:,3) = frm(tf);
      pix = sort(F(:));
      bgcolor = pix(round(numel(pix)*0.95));
   end
   
   % if autocrop is turned on
   if crop
      tf = any(mask,1);
      
      Icols = [find(tf,1,'first')-1 find(tf,1,'last')];
      w = diff(Icols);
      if mod(w,2)
         w = w - 1;
         Icols(2) = Icols(2) - 1;
      end
      
      tf = any(mask,2);
      Irows = [find(tf,1,'first')-1 find(tf,1,'last')];
      h = diff(Irows);
      if mod(h,2)
         h = h - 1;
         Irows(2) = Irows(2) - 1;
      end
      
      % crop the mask
      mask = mask(Irows(1)+1:Irows(2),Icols(1)+1:Icols(2));
      maskfile = [tempname '.bmp'];
      delfcn = @()delete(maskfile);
   else
      h = size(mask,1);
      w = size(mask,2);
      if vr.Height~=h || vr.Width~=w
         error('Dimensions of the Mask differs from the frame size.');
      end
      Icols = 0;
      Irows = 0;
   end
   
   info = struct('crop',crop,'H',h,'W',w,'X',Icols(1),'Y',Irows(1),'T',vr.Duration,'R',vr.FrameRate,...
      'bgcolor',ffmpegcolor([bgcolor bgcolor bgcolor]));
   
   if isempty(delfcn)
      delfcn = @()[];
   else
      imwrite(mask,maskfile,'bmp')
   end
end

function filtdesc = create_filter(info)
filtdesc = sprintf('[1:v]alphamerge[f];color=c=%s:s=%dx%d:d=%0.3f:r=%f[b];[b][f]overlay',...
      info.bgcolor,info.W,info.H,info.T,info.R);
if info.crop
   filtdesc = sprintf('"[0:v]crop=%d:%d:%d:%d[v];[v]%s"',...
      info.W,info.H,info.X+1,info.Y+1,filtdesc);
else
   filtdesc = sprintf('"[0:v]%s"',filtdesc);
end
end
