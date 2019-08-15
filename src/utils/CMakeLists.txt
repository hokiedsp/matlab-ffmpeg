# BUILD ffmpeg.obj which is to be used by all the mex functions
target_sources(ffmpeg-utils PRIVATE ffmpegMxProbe.cpp ffmpeg_utils.cpp mxutils.cpp)

# set(LIBFFMPEG "libffmpeg")
# add_library(${LIBFFMPEG} OBJECT ffmpegBase.cpp ffmpegStream.cpp ffmpegStreamInput.cpp 
#                                 ffmpegFormatInput.cpp ffmpegDump.cpp ffmpegTimeUtil.h 
#                                 mxutils.cpp ffmpegException.cpp ffmpegReaderMT.cpp)

# add_subdirectory(filter) # adds additional gather source file lists in LIBFFMPEG_FILTERSRCS

# set(LIBFFMPEG ${LIBFFMPEG} PARENT_SCOPE)

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

  
# endif()
