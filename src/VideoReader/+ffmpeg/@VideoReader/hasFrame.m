function flag = hasFrame(obj)
%HASFRAME Determine if there is a frame available to read from a video file
%
%   FLAG = HASFRAME(OBJ) returns TRUE if there is a video frame available
%   to read from the file. If not, it returns FALSE.
%
%   Example:
%       % Construct a multimedia reader object associated with file
%       'xylophone.mp4'.
%       vidObj = VideoReader('xylophone.mp4');
%
%       % Specify that reading should start at 0.5 seconds from the
%       % beginning.
%       vidObj.CurrentTime = 0.5;
%
%       % Create an axes
%       currAxes = axes;
%       
%       % Read video frames until available
%       while hasFrame(vidObj)
%           vidFrame = readFrame(vidObj);
%           image(vidFrame, 'Parent', currAxes);
%           currAxes.Visible = 'off';
%           pause(1/vidObj.FrameRate);
%       end
%
%   See also AUDIOVIDEO, MOVIE, VIDEOREADER,VIDEOREADER/READFRAME, MMFILEINFO.

flag = obj.mex_backend(obj.backend,'hasFrame');
