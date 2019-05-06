function info = ffmpegformats(varargin)
%FFMPEGFORMATS   Retrieves FFMpeg supported formats
%   FFMPEGFORMATS() displays all the supported formats.
%
%   T = FFMPEGFORMATS() returns table T listing the supported
%   formats. If Matlab version does not support table, a struct array 
%   is returned instead.
%
%   [...] = FFMPEGFORMATS('muxers') or [...] = FFMPEGFORMATS('demuxers') 
%   limits the output to include only muxers or demuxers.
%
%   [...] = FFMPEGFORMATS(...,'devices') limists the output to include only
%   the streaming devices. While FFMPEGFORMATS can only display all devices 
%   (no output argument), its struct output could be limited to only contain
%   mux or demux devices.
%
%   See Also: FFMPEGSETUP, FFMPEGTRANSCODE

% Copyright 2015-2019 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
% rev. 1 : (05-04-2019) Major update:
%          - Additional input arguments to limit output listing
%          - Backend MEX implementation if struct is requested
%          - Returns table rather than struct

narginchk(0,2);
nargoutchk(0,1);

if nargin>0
    varargin(:) = cellfun(@(c)validatestring(c,{'muxers','demuxers','devices'}),varargin,'UniformOutput',false);
    tf = ismember({'muxers','demuxers','devices'}, varargin);
end

% run FFmpeg
if nargout==0
    % ffmpeg can only show all formats, all muxers, all demuxers, or all devices
    if nargin==0 || all(tf==[true true false])
        [~,msg] = system([ffmpegpath() ' -formats']);
    elseif tf(3)
        if sum(tf)>1
            warning('Direct FFmpeg output can only show all (i.e., both mux and demux) devices.');
        end
        [~,msg] = system([ffmpegpath() ' -devices']);
    elseif tf(1)
        [~,msg] = system([ffmpegpath() ' -muxers']);
    else
        [~,msg] = system([ffmpegpath() ' -demuxers']);
    end

    I = regexp(msg,'(?:File|Devices).+','once');

    disp(msg(I:end));
else
    % call private mex function to retrieve the info struct
    if nargin==0
        tf = false(1,3);
    end
    info = ffmpegformats_mex(tf(1),tf(2),tf(3));
    
    % attempt to return the results in table format
    try
        info = struct2table(info);
    catch
        
    end
end
