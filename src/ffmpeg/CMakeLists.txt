# BUILD ffmpeg.obj which is to be used by all the mex functions
add_library(ffmpeg OBJECT FFmpegMxProbe.cpp ffmpeg_utils.cpp avexception.cpp mxutils.cpp)
set(ffmpeg ${ffmpeg} PARENT_SCOPE) # make the library target available at the root level

set(LIBFFMPEG "libffmpeg")
add_library(LIBFFMPEG OBJECT ffmpegBase.cpp ffmpegStream.cpp ffmpegFormatInput.cpp ffmpegDump.cpp ffmpegReader.cpp)
set(LIBFFMPEG ${LIBFFMPEG} PARENT_SCOPE)

# if(TARGET ${LIBFFMPEG})

#   message("${LIBFFMPEG} is already defined")

# else()

#   # set(FFMPEG_SRCS ./src/ffmpegBase.cpp)
#   file(GLOB FFMPEG_SRCS "*.cpp")
  
#   # remove source files not quite ready
#   # list(REMOVE_ITEM FFMPEG_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/ffmpegMediaReader.cpp")
#   list(REMOVE_ITEM FFMPEG_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/ffmpegFilterGraph.cpp")
#   list(REMOVE_ITEM FFMPEG_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/ffmpegUtil.cpp")

#   include_directories(${FFMPEG_INCLUDE_DIR})
#   add_library(${LIBFFMPEG} OBJECT ${FFMPEG_SRCS})
  
#   set(LIBFFMPEG ${LIBFFMPEG} PARENT_SCOPE)

  add_subdirectory(filter)
#   set(LIBFFMPEGFILTER ${LIBFFMPEGFILTER} PARENT_SCOPE)
  
# endif()
