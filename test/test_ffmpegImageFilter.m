clear; close all;

formats = ffmpeg.ImageFilter.getFormats();
filters = ffmpeg.ImageFilter.getFilters();

imgfilter = ffmpeg.ImageFilter('scale=640:360');
imdata = imread('ngc6543a.jpg');
filtdata = imgfilter.run(imdata);
filtdata = imgfilter.run(imdata); % run twice to verify filter state flushing
disp('passed simple filter graph test');

% test complex filter
% > https://superuser.com/questions/916431/ffmpeg-filter-to-boxblur-and-greyscale-a-video-using-alpha-mask
% > https://superuser.com/questions/901099/ffmpeg-apply-blur-over-face
imgfilter = ffmpeg.ImageFilter('[in][mask]alphamerge,hue=s=0,boxblur=5[fg]; [in][fg]overlay');

[X,map] = imread('corn.tif');
Im = ind2rgb(X,map);
mask = repmat(Im(:,:,1),1,1,2);
mask(:,:,2) = Im(:,:,1)>0.5;

imgfilter.InputFormat = struct('in','rgb24','mask','gray8a');
out = imgfilter.run('in',Im,'mask',mask);

imshow(out)
