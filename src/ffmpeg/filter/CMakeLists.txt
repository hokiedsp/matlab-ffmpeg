cmake_minimum_required(VERSION 3.3)

# set(LIBFFMPEGFILTER "libffmpegfilter")

# set(FFMPEG_SRCS ./src/ffmpegBase.cpp)
file(GLOB LIBFFMPEGFILTER_SRCS "*.cpp")

# remove source files not quite ready
# list(REMOVE_ITEM LIBFFMPEGFILTER_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/ffmpegFilterGraph.cpp")

add_library(${LIBFFMPEG} OBJECT ${LIBFFMPEGFILTER_SRCS})

# set(LIBFFMPEGFILTER ${LIBFFMPEGFILTER} PARENT_SCOPE)
