function formats = getVideoCompressions()
%FFMPEG.VIDEOREADER.GETVIDEOCOMPRESSIONS   Get supported video codecs
%   CODECS = FFMPEG.VIDEOREADER.GETVIDEOCOMPRESSIONS() returns a struct
%   array of supported video codecs (only video decoders).
%
%   The fields of the returned struct array are:
%
%   Name           - Name of the codec
%   Lossless       - 'on' if supports lossless compression
%   Lossy          - 'on' if supports lossy compression
%   IntraframeOnly - 'on' if only supports intra-frame compression
%   Description    - Long name of the codec
%
%   See also: ffmpeg.VideoReader.getFileFormats, ffmpeg.VideoReader.getVideoFormats

ffmpegsetenv();
formats = ffmpeg.VideoReader.mex_backend([], 'static', mfilename);

if nargout==0
   display(struct2table(formats));
   clear formats
end
