@echo off
REM C++ Mex Building batch file
CALL "C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
REM CALL "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64

set MEX_NAME=ffmpegfileinfo
set MEX_DIR=
set MEX_SRC_FILES=mex_ffmpegfileinfo.cpp ffmpegFileDump.cpp ..\Common\ffmpegBase.cpp

set MEX_EXT=.mexw64
set MATLAB_ROOT_DIR=%ProgramFiles%\MATLAB\R2017a
set MATLAB_EXTERN_DIR=%MATLAB_ROOT_DIR%\extern
set MATLAB_INCLUDE_DIR=%MATLAB_EXTERN_DIR%\include
set MATLAB_LIB_DIR=%MATLAB_EXTERN_DIR%\lib\win64\microsoft
set MATLAB_LIB_FILES=libmx.lib libmex.lib libmat.lib

set FFMPEG_ROOT_DIR=%USERPROFILE%\Documents\Programming\ffmpeg-3.3.3
set FFMPEG_INCLUDE_DIR=%FFMPEG_ROOT_DIR%\include
set FFMPEG_LIB_DIR=%FFMPEG_ROOT_DIR%\lib
set FFMPEG_BIN_DIR=%FFMPEG_ROOT_DIR%\bin
set FFMPEG_LIB_FILES=avcodec.lib avdevice.lib avfilter.lib avformat.lib avutil.lib postproc.lib swresample.lib swscale.lib Shell32.lib

REM set compilerflags=/O2 /EHsc /I"%MATLAB_INCLUDE_DIR%" /D%MATLAB_PREPROC%
set compilerflags=/GR /W3 /EHs /D_CRT_SECURE_NO_DEPRECATE /D_SCL_SECURE_NO_DEPRECATE /D_SECURE_SCL=0 /DMATLAB_MEX_FILE /nologo /MD /I"%MATLAB_INCLUDE_DIR%" /I"%FFMPEG_INCLUDE_DIR%"
set morecompflags=/O2 /Oy- /DNDEBUG REM optimization
rem set morecompflags=/Z7 REM for debugging
set linkerflags=/DLL /machine:x64 /EXPORT:mexFunction /LIBPATH:"%MATLAB_LIB_DIR%" /LIBPATH:"%FFMPEG_LIB_DIR%" /OUT:"%MEX_DIR%%MEX_NAME%%MEX_EXT%"
REM set linkerdebugflags=/debug /PDB:"%MEX_NAME%%MEX_EXT%.pdb"
set linkerdebugflags=

cl.exe %compilerflags% %morecompilerflags% %MEX_SRC_FILES% /link %linkerflags% %linkerdebugflags% %MATLAB_LIB_FILES% %FFMPEG_LIB_FILES%
@if ERRORLEVEL == 0 (  
   REM Delete intermediate/log files
   del /q "%MEX_DIR%%MEX_NAME%.exp" "%MEX_DIR%%MEX_NAME%.lib" *.obj

   echo BUILD COMPLETE
   goto end
)  

@if ERRORLEVEL != 0 (  
   echo BUILD FAILED
   goto end
)  

:end