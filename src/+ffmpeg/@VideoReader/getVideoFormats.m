function formats = getVideoFormats()
%FFMPEG.VIDEOREADER.GETVIDEOFORMATS   Get supported video pixel formats
%   FORMATS = FFMPEG.VIDEOREADER.GETVIDEOFORMATS() returns a struct
%   array of supported video formats (FFmpeg's PixelFormat). Any of the
%   listed format could be used for a ffmpeg.VideoReader object by
%   specifying 'VideoFormat' option in the constructor argument.
%
%   The fields of the returned struct array are:
%
%   Name               - Name of the PixelFormat
%   Alias              - Other accepted name of the format
%   NumberOfComponents - Number of Video Components (frames' 3rd dimension)
%   BitsPerPixel       - Number of bits to represent one bit
%   RGB                - 'on' if contains RGB-like data; 'off' if YUV/grayscale
%   Alpha              - 'on' if supports alpha channel
%   Paletted           - 'on' if a color palet is also given, 'psudo' if uses a fixed palette
%   HWAccel            - 'on' if HW accelerated
%   Bayer              - 'on' if follows Bayer pattern
%   Log2ChromaW        - Amount to shift the luma width right to find the chroma width
%   Log2ChromaH        - Amount to shift the luma height right to find the chroma height
%   
%   See also: ffmpeg.VideoReader.getFileFormats,
%             ffmpeg.VideoReader.getVideoCompressions


ffmpegsetenv();
formats = ffmpeg.VideoReader.mex_backend([], 'static', mfilename);

if nargout==0
   display(struct2table(formats));
   clear formats
end
