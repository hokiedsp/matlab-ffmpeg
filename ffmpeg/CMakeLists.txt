# BUILD ffmpeg.obj which is to be used by all the mex functions
add_library(ffmpeg OBJECT FFmpegInputFile.cpp FFmpegInputStream.cpp ffmpeg_utils.cpp avexception.cpp mxutils.cpp)

# make the library target available at the root level
set(ffmpeg ${ffmpeg} PARENT_SCOPE)
