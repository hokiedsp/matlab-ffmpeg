function formats = getFileFormats()
%FFMPEG.GETFILEFORMATS   Get supported video file formats
%   FORMATS = FFMPEG.VIDEOREADER.GETFILEFORMATS() returns a struct
%   array of FileFormatInfo objects which are the
%   formats FFMPEG.VIDEOREADER is known to support on the current
%   platform.
%
%    The properties of an audiovideo.FileFormatInfo object are:
%
%   Extension   - The file extension for this file format Description - A
%   text description of the file format ContainsVideo - The File Format can
%   hold video data ContainsAudio - The File Format can hold audio data

ffmpegsetenv();
formats = ffmpeg.VideoReader.mex_backend([], 'static', mfilename);

if nargout==0
   display(struct2table(formats));
   clear formats
end
% extensions = audiovideo.mmreader.getSupportedFormats();
% formats = audiovideo.FileFormatInfo.empty();
% for ii=1:length(extensions)
%    formats(ii) = audiovideo.FileFormatInfo( extensions{ii}, ...
%       VideoReader.translateDescToLocale(extensions{ii}), ...
%       true, ...
%       false );
% end
% 
% % sort file extension
% [~, sortedIndex] = sort({formats.Extension});
% formats = formats(sortedIndex);
