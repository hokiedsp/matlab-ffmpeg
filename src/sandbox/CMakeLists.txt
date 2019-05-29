# BUILD ffmpeg.obj which is to be used by all the mex functions
add_library(transcode OBJECT transcode_inputstream.cpp transcode_inputfile.cpp)
# transcode.cpp   transcode_outputfile.cpp transcode_outputstream.cpp transcode_hw.cpp transcode_filter.cpp transcode_utils.cpp

# make the library target available at the root level
set(ffmpeg ${ffmpeg} PARENT_SCOPE)

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

#   add_subdirectory(filter)
#   set(LIBFFMPEGFILTER ${LIBFFMPEGFILTER} PARENT_SCOPE)
  
# endif()
