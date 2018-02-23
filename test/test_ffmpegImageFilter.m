clear; close all;

formats = ffmpeg.ImageFilter.getFormats();
filters = ffmpeg.ImageFilter.getFilters();

imgfilter = ffmpeg.ImageFilter('scale=640:360','AutoTranspose',true);
imdata = imread('ngc6543a.jpg');

[X,map] = imread('corn.tif');
imdata2 = ind2rgb(X,map);

disp('RUNNING simple filter graph test');
filtdata = imgfilter.run(imdata);
% disp('RE-RUNNING simple filter graph test');
% filtdata = imgfilter.run(imdata); % run twice to verify filter state flushing
% disp('RE-RUNNING simple filter graph test with different image');
% filtdata = imgfilter.run(imdata2); % run twice to verify filter state flushing
% disp('passed simple filter graph test');

% test complex filter
% > https://superuser.com/questions/916431/ffmpeg-filter-to-boxblur-and-greyscale-a-video-using-alpha-mask
% > https://superuser.com/questions/901099/ffmpeg-apply-blur-over-face
% imgfilter = ffmpeg.ImageFilter('[in][mask]alphamerge,hue=s=0,boxblur=5[fg]; [in][fg]overlay,format=rgb24');

% mask = imdata2(:,:,1)>0.5;

% imgfilter.InputFormat = struct('in','rgb24','mask','gray8');
% [out,out_format] = imgfilter.run('in',imdata2,'mask',mask);

% out_format
% imshow(out.out(:,:,1:3))
