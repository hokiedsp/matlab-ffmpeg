clear; close all;

formats = ffmpeg.ImageFilter.getFormats();
filters = ffmpeg.ImageFilter.getFilters();

imgfilter = ffmpeg.ImageFilter('scale=640:360');
imdata = imread('ngc6543a.jpg');
filtdata = imgfilter.run(imdata);

