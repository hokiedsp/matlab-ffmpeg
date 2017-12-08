function formats = getVideoFormats()
%FFMPEG.GETVIDEOFORMATS   Get supported video pixel formats
%   FORMATS = FFMPEG.VIDEOREADER.GETVIDEOFORMATS() returns a struct
%   array of supported video formats (FFmpeg's PixelFormat). Any of the
%   listed format could be used for a ffmpeg.VideoReader object by
%   specifying 'VideoFormat' option in the constructor argument.
%
%   The fields of the returned struct array are:
%
%   Name               - Name of the PixelFormat
%   Alias
%   NumberOfComponents - Number of Video Components (frames' 3rd dimension)
%   BitsPerPixel
%   RGB
%   Alpha
%   Palletted
%   HWAccel
%   Bayer
%   Log2ChromaW
%   Log2ChromaH
%   
%   hold video data ContainsAudio - The File Format can hold audio data

ffmpegsetenv();
formats = ffmpeg.VideoReader.mex_backend([], 'static', mfilename);

if nargout==0
   fnames = fieldnames(formats);
%    formats = rmfield(formats,setdiff(fnames,{'Name','RGB','BitsPerPixel','Alpha'}));
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
