clear; close all;

% formats = ffmpeg.ImageFilter.getFormats();
% filters = ffmpeg.ImageFilter.getFilters();

% imgfilter = ffmpeg.ImageFilter('scale=640:360','AutoTranspose',true);
imdata = imread('ngc6543a.jpg');

[X,map] = imread('corn.tif');
imdata2 = ind2rgb(X,map);

% disp('RUNNING simple filter graph test');
% filtdata = imgfilter.run(imdata);
% disp('RE-RUNNING simple filter graph test');
% filtdata = imgfilter.run(imdata); % run twice to verify filter state flushing
% disp('RE-RUNNING simple filter graph test with different image');
% filtdata = imgfilter.run(imdata2); % run twice to verify filter state flushing
% disp('passed simple filter graph test');

% test complex filter
% > https://superuser.com/questions/916431/ffmpeg-filter-to-boxblur-and-greyscale-a-video-using-alpha-mask
% > https://superuser.com/questions/901099/ffmpeg-apply-blur-over-face
% imgfilter = ffmpeg.ImageFilter('[in][mask]alphamerge,hue=s=0,boxblur=5[fg]; [in][fg]overlay,format=rgb24');
imgfilter = ffmpeg.ImageFilter('[in]boxblur=10[bg];[in]crop=200:200:60:30[fg];[bg][fg]overlay=60:30');
imgfilter.OutputFormat = 'rgb24';
filtdata = imgfilter.run(imdata2); % run twice to verify filter state flushing
figure; imshow(filtdata); title(sprintf('AutoTranspose:%d | OutputFormat:%s',imgfilter.AutoTranspose,imgfilter.OutputFormat));
imgfilter.AutoTranspose = false;
filtdata = imgfilter.run(imdata2); % run twice to verify filter state flushing
figure; imshow(filtdata); title(sprintf('AutoTranspose:%d | OutputFormat:%s',imgfilter.AutoTranspose,imgfilter.OutputFormat));

imgfilter.FilterGraph = ['nullsrc=size=200x100,split[bg0][bg1];' ...
'[in0] scale=100x100,split[l0][l1];' ...
'[in1] scale=100x100,split[r0][r1];'...
'[bg0][l0]      overlay=shortest=1       [bg+left];'...
'[bg+left][r0]  overlay=shortest=1:x=100 [leftright];'...
'[bg1][r1]      overlay=shortest=1       [bg+right];'...
'[bg+right][l1] overlay=shortest=1:x=100 [rightleft]'];
imgfilter.AutoTranspose = true;
filtdata = imgfilter.run('in0',imdata2,'in1',imdata);
figure; subplot(211);imshow(filtdata.leftright);
subplot(212); imshow(filtdata.rightleft);
% mask = imdata2(:,:,1)>0.5;

% imgfilter.InputFormat = struct('in','rgb24','mask','gray8');
% [out,out_format] = imgfilter.run('in',imdata2,'mask',mask);

% out_format
% imshow(out.out(:,:,1:3))
