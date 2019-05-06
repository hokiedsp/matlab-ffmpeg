function formats = getFileFormats()
%FFMPEG.VIDEOREADER.GETFILEFORMATS   Get supported video file formats
%   FORMATS = FFMPEG.VIDEOREADER.GETFILEFORMATS() returns a struct
%   array of FileFormatInfo objects which are the
%   formats FFMPEG.VIDEOREADER is known to support on the current
%   platform.
%
%   The properties of an audiovideo.FileFormatInfo object are:
%
%   Names            - Comma separated list of names
%   Description      - Format description
%   Extensions       - Comma separated list of file extensions (not exhaustive)
%   MIMETypes        - Comma-separated list of mime types
%
%   See also: ffmpeg.VideoReader.getVideoFormats,
%             ffmpeg.VideoReader.getVideoCompressions

ffmpegsetenv();
formats = ffmpeg.VideoReader.mex_backend([], 'static', mfilename);

if nargout==0
   display(struct2table(formats));
   clear formats
end
