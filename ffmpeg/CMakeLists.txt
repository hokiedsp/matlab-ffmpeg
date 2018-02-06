cmake_minimum_required(VERSION 3.3)

set(LIBFFMPEG_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)

set(LIBFFMPEG "libffmpeg")

if(TARGET ${LIBFFMPEG})

  message("${LIBFFMPEG} is already defined")

else()

  # set(FFMPEG_SRCS ./src/ffmpegBase.cpp)
  file(GLOB FFMPEG_SRCS "*.cpp")
  
  # remove source files not quite ready
  # list(REMOVE_ITEM FFMPEG_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/ffmpegMediaReader.cpp")
  list(REMOVE_ITEM FFMPEG_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/ffmpegFilterGraph.cpp")
  list(REMOVE_ITEM FFMPEG_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/ffmpegUtil.cpp")

  include_directories(${FFMPEG_INCLUDE_DIR})
  add_library(${LIBFFMPEG} OBJECT ${FFMPEG_SRCS})
  
  set(LIBFFMPEG ${LIBFFMPEG} PARENT_SCOPE)

  add_subdirectory(filter)
  set(LIBFFMPEGFILTER ${LIBFFMPEGFILTER} PARENT_SCOPE)
  
endif()


