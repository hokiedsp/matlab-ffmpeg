cmake_minimum_required(VERSION 3.3)

# set(FFMPEG_SRCS ./src/ffmpegBase.cpp)
file(GLOB LIBFFMPEG_FILTERSRCS "*.cpp")
target_sources(${LIBFFMPEG} PUBLIC ${LIBFFMPEG_FILTERSRCS})
