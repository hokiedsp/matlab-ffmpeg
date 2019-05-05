function info = ffmpegcodecs(varargin)
%FFMPEGCODECS   Retrieves FFMpeg supported codecs
%   FFMPEGCODECS() displays all the supported codecs.
%
%   T = FFMPEGCODECS() returns table T listing the supported
%   codecs. If Matlab version does not support table, a struct array 
%   is returned instead.
%
%   [...] = FFMPEGCODECS('encoders') or [...] = FFMPEGCODECS('decoders') 
%   limits the output to include only encoders or decoders. Setting this 
%   option returns additional output fields.
%
%   S = FFMPEGCODECS(...,'video'), S = FFMPEGCODECS(...,'audio'),
%   S = FFMPEGCODECS(...,'subtitle'), or S = FFMPEGCODECS(...,'other') limists 
%   the output to include only the specified media types. This option is only
%   available when returning a struct output.
%
%   See Also: FFMPEGSETUP, FFMPEGTRANSCODE, FFMPEGFORMATS

% Copyright 2015-2019 Takeshi Ikuma
% History:
% rev. - : (04-06-2015) original release
% rev. 1 : (05-04-2019) Major update:
%          - Additional input arguments to limit output listing
%          - Backend MEX implementation if struct is requested
%          - Returns table rather than struct

nargoutchk(0,1);

if nargin>0
    options = {'encoders','decoders','video','audio','subtitle','other'};
    varargin(:) = cellfun(@(c)validatestring(c,options),varargin,'UniformOutput',false);
    tf = ismember(options, varargin);
    if ~any(tf([1 2]))
        tf([1 2]) = true;
    end
    if ~any(tf(3:end))
        tf(3:end) = true;
    end
else
    tf = true(1,6);
end

% run FFmpeg
if nargout==0
    if ~all(tf([3:end]))
        warning('Direct FFmpeg output can only list all media types.');
    end

    if nargin==0 || all(tf([1 2]))
        [~,msg] = system([ffmpegpath() ' -codecs']);
    elseif tf(1)
        [~,msg] = system([ffmpegpath() ' -encoders']);
    else
        [~,msg] = system([ffmpegpath() ' -decoders']);
    end

    I = regexp(msg,'Codecs|Encoders|Decoders','once');

    disp(msg(I:end));
else
    % call private mex function to retrieve the info struct
    info = ffmpegcodecs_mex(tf(1),tf(2),tf(3),tf(4),tf(5),tf(6));

    % attempt to return the results in table format
    try
        info = struct2table(info);
    catch
        
    end
end
