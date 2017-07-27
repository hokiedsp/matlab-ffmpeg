@echo off
REM C++ Mex Building batch file
CALL "C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
REM CALL "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64

REM ffmpegBase.cpp ffmpegOptionParseContext.cpp ffmpegOption.cpp   ffmpegOptionsContextInput.cpp ffmpegOptionsContext.cpp ffmpegOptionDefs.cpp ffmpegInputStream.cpp
REM ffmpegOptionDefs.cpp
REM cmdutils.c

set MEX_NAME=mexFFmpegVideoReaderGetFileFormats
set MEX_SRC_FILES=mex_ffmpeg_getfileformats.cpp
REM ffmpegInputStreamSubtitle.cpp
REM main.cpp ffmpegOption.cpp ffmpegOptionsContext.cpp ffmpegOptionsContextInput.cpp ffmpegBase.cpp ffmpegInputFile.cpp ffmpegFilterGraph.cpp ffmpegInputStream.cpp ffmpegInputStreamAudio.cpp ffmpegInputStreamVideo.cpp

set MEX_EXT=.mexw64
set MATLAB_ROOT_DIR=C:\Program Files\MATLAB\R2017a
set MATLAB_EXTERN_DIR=%MATLAB_ROOT_DIR%\extern
set MATLAB_INCLUDE_DIR=%MATLAB_EXTERN_DIR%\include
set MATLAB_LIB_DIR=%MATLAB_EXTERN_DIR%\lib\win64\microsoft
set MATLAB_LIB_FILES=libmx.lib libmex.lib libmat.lib

REM set FFMPEG_ROOT_DIR=D:\Users\Kesh\Documents\Programming\ffmpeg-3.2.4
set FFMPEG_ROOT_DIR=C:\Users\TakeshiIkuma\Documents\Programming\ffmpeg-3.3.2
set FFMPEG_INCLUDE_DIR=%FFMPEG_ROOT_DIR%\include
set FFMPEG_LIB_DIR=%FFMPEG_ROOT_DIR%\lib
set FFMPEG_BIN_DIR=%FFMPEG_ROOT_DIR%\bin
set FFMPEG_LIB_FILES=avcodec.lib avdevice.lib avfilter.lib avformat.lib avutil.lib postproc.lib swresample.lib swscale.lib Shell32.lib

REM set compilerflags=/O2 /EHsc /I"%MATLAB_INCLUDE_DIR%" /D%MATLAB_PREPROC%
set compilerflags=/GR /W3 /wd4819 /EHs /D_CRT_SECURE_NO_DEPRECATE /D_SCL_SECURE_NO_DEPRECATE /D_SECURE_SCL=0 /D_WIN32 /D__STDC_CONSTANT_MACROS /nologo /MD /I"%FFMPEG_INCLUDE_DIR%" /I"%MATLAB_INCLUDE_DIR%" /DMATLAB_MEX_FILE 
set morecompflags=/O2 /Oy- /DNDEBUG REM optimization
rem set morecompflags=/Z7 REM for debugging
set linkerflags=/machine:x64 /DLL /LIBPATH:"%FFMPEG_LIB_DIR%" /EXPORT:mexFunction /OUT:%MEX_NAME%%MEX_EXT% /LIBPATH:"%MATLAB_LIB_DIR%"
REM set linkerdebugflags=/debug /PDB:"%MEX_NAME%%MEX_EXT%.pdb"
set linkerdebugflags=

cl.exe %compilerflags% %morecompilerflags% %MEX_SRC_FILES% /link %linkerflags% %linkerdebugflags% %FFMPEG_LIB_FILES% %MATLAB_LIB_FILES%

del *.obj

echo BUILD COMPLETE
