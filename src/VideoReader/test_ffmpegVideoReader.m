clear; close all; drawnow

%Print all supported video file formats
% ffmpeg.VideoReader.getFileFormats
% ffmpegtranscode('xylophone.mp4','test.mp4')
try
%    vrobj = ffmpeg.VideoReader('xylophone.mp4');
%    vrobj = ffmpeg.VideoReader('test.mp4');
   vrobj = ffmpeg.VideoReader('E:\HSV Data\Vision Research Test 2.avi');
      fprintf('VideoCompression: %s\n',vrobj.VideoCompression);
%       fprintf('Setting CurrentTime to 2.0...\n');
   %    vrobj.CurrentTime = 2.0;
%       fprintf('New CurrentTime = %g\n',vrobj.CurrentTime);
%    disp('Reading the first frame');
   frame = vrobj.readFrame();
   pause(1)
   disp('deleting the object');
   delete(vrobj);
catch ME
   disp(ME.getReport());
   delete(vrobj);
end
clear ffmpeg.VideoReader mex_backend
