clear; close all;

formats = ffmpeg.ImageFilter.getFormats();
filters = ffmpeg.ImageFilter.getFilters();

imgfilter = ffmpeg.ImageFilter('scale=640:360');
imdata = imread('ngc6543a.jpg');
filtdata = imgfilter.run(imdata);
filtdata = imgfilter.run(imdata); % run twice to verify filter state flushing

% test complex filter
% > https://superuser.com/questions/916431/ffmpeg-filter-to-boxblur-and-greyscale-a-video-using-alpha-mask
% > https://superuser.com/questions/901099/ffmpeg-apply-blur-over-face
imgfilter = ffmpeg.ImageFilter('[in][mask]alphamerge,hue=s=0,boxblur=5[fg]; [in][fg]overlay');
