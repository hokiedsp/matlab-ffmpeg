# matlab-ffmpeg: FFmpeg Toolbox for MATLAB

FFmpeg (http://ffmpeg.org/) is a complete, cross-platform solution to record, convert and stream audio and video. FFmpeg Toolbox is aimed to bring FFmpeg features to Matlab. 

There are 2 branches of work included in this project.

1. A set of wrapper m-functions to run FFmpeg binary from Matlab primarily for transcoding
2. A set of Matlab classes directly interfacing with FFmpeg shared library via C++ MEX functions for multimedia file IO and filtering

The former part has been available at [Matlab File Exchange] (https://www.mathworks.com/matlabcentral/fileexchange/42296) which will be migrated to GitHub once the second part became stable enough to be posted on the File Exchange. 

## DEPENDENCIES
* [FFmpeg](https://ffmpeg.org/) (need both binaries and shared libraries)
* [CMake](https://cmake.org/) for building and installing the project

(for prebuilt binaries for Windows and OSX, visit to https://ffmpeg.zeranoe.com/builds/, download both Shared and Dev "Linking" options, unzip them in a same directory)

## TIPS TO BUILDING THE PROJECT
First, the project is developed under Windows. User help is needed especially to address issues in the non-Windows OSes
