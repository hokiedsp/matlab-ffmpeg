function varargout = run(obj,varargin)
%FFMPEG.IMAGEFILTER.RUN   Run the filter
%   B = RUN(OBJ,A) runs a filter chain (i.e., single input, single output
%   filter graph) defined in OBJ with the input image A  and returns the
%   ouptut image B.
%
%   A can be an M-by-N (grayscale image) or M-by-N-by-3 (color RGB image)
%   array.  A cannot be an empty array or sparse. The input array A can be
%   of class logical, uint8, uint16, single, or double. 
%
%   Indexed images (X) can be of
%   class uint8, uint16, single, or double; the associated colormap, MAP, must be double.  

If the format specified is TIFF,
% imwrite can also accept an M-by-N-by-4 array containing color data
% that uses the CMYK color space.