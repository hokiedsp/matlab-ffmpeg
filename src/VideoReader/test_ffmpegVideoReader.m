clear; close all; drawnow

%Print all supported video file formats
% ffmpeg.VideoReader.getFileFormats
% ffmpegtranscode('xylophone.mp4','test.mp4')
try
   %       vrobj = ffmpeg.VideoReader('xylophone.mp4');
   vrobj = ffmpeg.VideoReader('..\test.mp4','VideoFormat','rgb24')
   %    vrobj = ffmpeg.VideoReader('E:\HSV Data\Vision Research Test 2.avi')
   %    fprintf('VideoCompression: %s\n',vrobj.VideoCompression);
   %       fprintf('Setting CurrentTime to 2.0...\n');
   %    vrobj.CurrentTime = 2.0;
   %       fprintf('New CurrentTime = %g\n',vrobj.CurrentTime);
   %    disp('Reading the first frame');

%    for n = 1:10
      [frames,t] = vrobj.readBuffer();
%    end
      vrobj
      vrobj.CurrentTime = 40;
      [frames1,t1] = vrobj.readBuffer();
%    frames = zeros(vrobj.Height, vrobj.Width, 3, 10,'uint8');
%    for n = 1:5
%       % t(n) = vrobj.CurrentTime;
%       frames(:,:,:,n) = vrobj.readFrame();
%    end
      vrobj
   
%     pause(1)
   %  disp('deleting the object');
   delete(vrobj);
catch ME
   disp(ME.getReport());
   try delete(vrobj);
   catch, end
end
clear ffmpeg.VideoReader mex_backend
