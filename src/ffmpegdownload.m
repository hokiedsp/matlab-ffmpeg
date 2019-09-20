function ffmpegdownload(varargin)
   %FFMPEGDOWNLOAD   Download FFmpeg binary files from online repository
   %   FFMPEGDOWNLOAD checks online server (ffmpeg.zeranoe.com) for the latest
   %   release of FFmpeg for Windows or Mac OSX and download one if necessary.
   %   FFmpeg must be preinstalled in Linux.
   %
   %   FFMPEGDOWNLOAD(dstdir) downloads the latest release to the directiory,
   %   specified by dstdir. Toolbox will not use the downloaded version.
   %
   %   FFMPEGDOWNLOAD('dstdir',version,type) specifies which version and build
   %   type to download. The version name may be 'lastrelease', 'latest', or
   %   any valid build name ('latest' grabs the latest nightly build while
   %   'lastrelease gets the last release with a version number). The type
   %   may be 'shared' (default) or 'static'. For example, version='3.2' and
   %   type='static' downloads the static build of FFmpeg v.3.2.
   %
   %   If it is desiable for the FFmpeg toolbox to use an older release, set
   %   dstdir = [] and specify the version. In this case, type must be
   %   'shared' as its MEX functions uses the shared libraries.
   
   % Copyright 2019 Takeshi Ikuma
   % History:
   % rev. - : (05-01-2019) original release
   % rev. 1 : (09-09-2019) Changed binary files to be stored under bin subfolder
   
   [dstdir,ffmpegver,type,baseurl] = parseinputs(varargin);
   
   % get web distribution directory listing
   if ispc()
      os = 'win64';
   elseif ismac()
      os = 'macos64';
   else
      error('ffmpegdownload is only for Windows and Mac. Please manually install ffmpeg and its shared library packages.');
   end
   
   toolboxdownload = isempty(dstdir); % true if downloading to add files to the toolbox
   
   fprintf('Downloading the list of available FFmpeg builds for %s...',os);
   
   % download the listing of available builds
   webdir = [baseurl '/' os '/' type '/'];
   dirfile = websave([tempdir 'shareddir.html'],webdir);
   cleanup_dirfile = onCleanup(@()delete(dirfile));
   
   % parse the listing HTML
   dirlisting = fileread(dirfile);
   toks = regexp(dirlisting,'<tr><td><a.*?>(?<zipfile>ffmpeg.+?)</a></td><td>.+?</td><td>(?<datestr>\d{4}.+?)</td></tr>','names');
   if isempty(toks)
      error('FFmpeg build zip files not found at %s', webdir);
   end
   
   fprintf(' done\n');
   
   % find the requested version
   if strcmp(ffmpegver,'lastrelease')
      I = find(~cellfun(@isempty,regexp({toks.zipfile}','^ffmpeg-\d+\.\d+(?\.\d+)?','once')));
      builddate = datenum({toks(I).datestr},'yyyy-mm-dd HH:MM');
      [builddate,J] = max(builddate);
      zipfilename = toks(I(J)).zipfile;
      ffmpegver = char(regexp(zipfilename,['^ffmpeg-(.+?)-' os '-' type '.zip$'],'tokens','once'));
   else
      zipfilename = sprintf('ffmpeg-%s-%s-%s.zip',ffmpegver,os,type);
      I = strcmp({toks.zipfile},zipfilename);
      if ~any(I)
         error('Specified version of FFmpeg does not exist on this server: %s', zipfilename);
      end
      builddate = datenum(toks(I).datestr,'yyyy-mm-dd HH:MM');
   end
   
   fprintf('Found %s FFmpeg version %s for %s\n',type,ffmpegver,os);
   
   % check if toolbox already is using the release
   if toolboxdownload && ispref('ffmpeg','builddate') ...
         && builddate==getpref('ffmpeg','builddate') ...
         && ispref('ffmpeg','exepath') && exist(getpref('ffmpeg','exepath'),'file')
      disp('FFmpeg Toolbox already uses the requested/latest executable. No need to download.');
      return;
   end
   
   % download & unzip ffmpeg binary ZIP file
   fprintf('Downloading & extracting the zip file...');
   zipurl = [webdir zipfilename];
   [~,buildname] = fileparts(zipfilename);
   unzipdir = [tempdir buildname];
   unzip(zipurl, unzipdir); % unzip directory from web to unzipdir
   cleanup_zipdir = onCleanup(@()rmdir(unzipdir,'s'));
   
   % move the files to
   unzipdirin = fullfile(unzipdir,buildname);
   if exist(unzipdirin,'file')
      unzipdir = unzipdirin;
   end
   if toolboxdownload % copy files to the toolbox private directory
      toolboxpath = fileparts(which(mfilename));
      % copy content of bin subfolder to private
      exepath = fullfile(toolboxpath,'bin');
      [SUCCESS,MESSAGE,MESSAGEID] = movefile(fullfile(unzipdir,'bin','*'),exepath);
      if ~SUCCESS
         error(MESSAGEID,MESSAGE);
      end
      [SUCCESS,MESSAGE,MESSAGEID] = movefile(fullfile(unzipdir,'presets'),toolboxpath);
      if ~SUCCESS
         error(MESSAGEID,MESSAGE);
      end
      
      % update ffmpeg preference
      exepath = fullfile(exepath,'ffmpeg');
      if ispc()
         exepath = [exepath '.exe'];
      end
      setpref('ffmpeg','exepath',exepath);
      setpref('ffmpeg','builddate',builddate);
   else
      [SUCCESS,MESSAGE,MESSAGEID] = movefile(unzipdir,dstdir);
      if ~SUCCESS
         error(MESSAGEID,MESSAGE);
      end
   end
   fprintf(' done\n');
   end
   
   function [dstdir,ffmpegver,type,baseurl] = parseinputs(varargin)
   
   p = inputParser;
   p.addOptional('dstdir','',@(c)isempty(c)||(ischar(c)&&isrow(c)));
   p.addOptional('ffmpegver','lastrelease',@(c)ischar(c)&&isrow(c));
   p.addOptional('type','shared',@(c)ischar(c)&&isrow(c));
   p.addParameter('baseurl','https://ffmpeg.zeranoe.com/builds',@(c)ischar(c)&&isrow(c));
   p.parse(varargin{:});
   
   res = p.Results;
   [dstdir,ffmpegver,type,baseurl] = deal(res.dstdir,res.ffmpegver,res.type,res.baseurl);
   
   try % get the version keywords
      ffmpegver = validatestring(ffmpegver,{'latest','lastrelease'});
   catch % else expect the string to match the version name on the zip filename
   end
   
   if isempty(dstdir)
      type = validatestring(type,{'shared'});
   else
      type = validatestring(type,{'shared','static'});
   end
   
   end
   