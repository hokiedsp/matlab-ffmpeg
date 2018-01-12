clear; close all; drawnow

%Print all supported video file formats
% ffmpeg.VideoReader.getFileFormats
% ffmpegtranscode('xylophone.mp4','test.mp4')
try
   vrobj = ffmpeg.VideoReader('xylophone.mp4','VideoFormat','rgb24','BufferSize',3,'Direction','forward');
   %    vrobj = ffmpeg.VideoReader('..\test.mp4','VideoFormat','rgb24')
   %    vrobj = ffmpeg.VideoReader('..\test.mp4','VideoFormat','rgb24','Width',320/240*120,'Height',120)
   %    vrobj = ffmpeg.VideoReader('..\test.mp4','VideoFormat','rgb24','PixelAspectRatio',[2 1],'Height',120)
   %    vrobj = ffmpeg.VideoReader('..\test.mp4','VideoFormat','rgb24','PixelAspectRatio',[2 1],'Width',100)
   %    vrobj = ffmpeg.VideoReader('..\test.mp4','VideoFormat','rgb24','Width',1,'Height',100)
   %    vrobj = ffmpeg.VideoReader('E:\HSV Data\Vision Research Test 2.avi')
   %    fprintf('VideoCompression: %s\n',vrobj.VideoCompression);
   %       fprintf('Setting CurrentTime to 2.0...\n');
   %    vrobj.CurrentTime = 2.0;
   %       fprintf('New CurrentTime = %g\n',vrobj.CurrentTime);
   %    disp('Reading the first frame');
   [~,t1] = vrobj.readBuffer();
   t = [];
   while (vrobj.hasFrame)
       t_last = t;
      [frames,t] = vrobj.readBuffer();
   end
   vrobj.hasFrame
%       pause(1)
%       [frames1,t1] = vrobj.readBuffer();
%       T = vrobj.Duration;
%       fs = vrobj.FrameRate;
%    vrobj.CurrentTime = 0;
%       [frames1,t1] = vrobj.readBuffer();
%    vrobj
%    for n = 1:10
%       [frames,t] = vrobj.readBuffer();
%    end
%    vrobj.CurrentTime = vrobj.Duration-3/vrobj.FrameRate;
%    vrobj.hasFrame
%    T = vrobj.Duration
%    fs = vrobj.FrameRate;
%    
%          [frames1,t1] = vrobj.readBuffer();
%    %    frames = zeros(vrobj.Height, vrobj.Width, 3, 10,'uint8');
%    %    for n = 1:5
%    %       % t(n) = vrobj.CurrentTime;
%    %       frames(:,:,:,n) = vrobj.readFrame();
%    %    end
%    %       vrobj
%    %       [f,tf] = vrobj.readFrame();
%    
%        pause(0.1)
   %  disp('deleting the object');
   delete(vrobj);
catch ME
   disp(ME.getReport());
   try delete(vrobj);
   catch, end
end
clear ffmpeg.VideoReader mex_backend
