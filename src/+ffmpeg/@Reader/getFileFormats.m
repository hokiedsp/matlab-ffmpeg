function formats = getFileFormats()
%ffmpeg.Reader.GETFILEFORMATS   Get supported video file formats
%   FORMATS = ffmpeg.Reader.GETFILEFORMATS() returns a struct
%   array of FileFormatInfo objects which are the
%   formats ffmpeg.Reader is known to support on the current
%   platform.
%
%   The properties of an audiovideo.FileFormatInfo object are:
%
%   Names            - Comma separated list of names
%   Description      - Format description
%   Extensions       - Comma separated list of file extensions (not exhaustive)
%   MIMETypes        - Comma-separated list of mime types
%
%   See also: ffmpeg.Reader.getVideoFormats,
%             ffmpeg.Reader.getVideoCompressions

ffmpegsetenv();
formats = ffmpeg.Reader.mex_backend([], 'static', mfilename);

if nargout==0
   display(struct2table(formats));
   clear formats
end
