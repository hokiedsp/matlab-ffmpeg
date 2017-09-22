clear; close all; drawnow

%Print all supported video file formats
% ffmpeg.VideoReader.getFileFormats

try
   vrobj = ffmpeg.VideoReader('xylophone.mp4');
   %    fprintf('VideoCompression: %s\n',vrobj.VideoCompression);
   %    fprintf('Setting CurrentTime to 2.0...\n');
   %    vrobj.CurrentTime = 2.0;
   %    fprintf('New CurrentTime = %g\n',vrobj.CurrentTime);
   for n = 1:100
      pause(0.1);
   end
   disp('deleting the object');
   delete(vrobj);
catch ME
   disp(ME.getReport());
   delete(vrobj);
end
clear ffmpeg.VideoReader mex_backend
