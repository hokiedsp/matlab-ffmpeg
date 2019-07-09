function formats = getVideoCompressions()
%ffmpeg.Reader.GETVIDEOCOMPRESSIONS   Get supported video codecs
%   CODECS = ffmpeg.Reader.GETVIDEOCOMPRESSIONS() returns a struct
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
%   See also: ffmpeg.Reader.getFileFormats, ffmpeg.Reader.getVideoFormats

ffmpegsetenv();
formats = ffmpeg.Reader.mex_backend(mfilename);

if nargout==0
   display(struct2table(formats));
   clear formats
end
