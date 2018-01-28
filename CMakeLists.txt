cmake_minimum_required(VERSION 3.3.0)
project(FFMPEG_TOOLBOX VERSION 0.0.0)

set(CUSTOM_CMAKE_DIR "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_MODULE_PATH ${CUSTOM_CMAKE_DIR} ${CMAKE_MODULE_PATH})

set(CMAKE_BUILD_TYPE "Release")

find_package(Matlab REQUIRED COMPONENTS MX_LIBRARY)
find_package(FFMPEG REQUIRED)

# should we use our own math functions

if (WIN32)
  set(FFMPEG_TOOLBOX_INSTALL_DIR "$ENV{USERPROFILE}/Documents/MATLAB/toolbox/ffmpeg_test")
else()
  set(FFMPEG_TOOLBOX_INSTALL_DIR "$ENV{home}/Documents/MATLAB/toolbox/ffmpeg")
endif()

set(FFMPEG_TOOLBOX_INSTALL_DIR ${FFMPEG_TOOLBOX_INSTALL_DIR} CACHE PATH "Matlab FFMPEG Toolbox Installation Directory")

set(CMAKE_INSTALL_PREFIX ${FFMPEG_TOOLBOX_INSTALL_DIR})

# add the install targets
install(DIRECTORY matlab/ DESTINATION "." FILES_MATCHING PATTERN "*.m" PATTERN "include" EXCLUDE)
# install(DIRECTORY matlab/ DESTINATION "." FILES_MATCHING PATTERN "include" EXCLUDE PATTERN "*.m ")

add_subdirectory(ffmpeg)
add_subdirectory(matlab)

