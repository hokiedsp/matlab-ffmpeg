cmake_minimum_required(VERSION 3.3.0)
project(matlab-ffmpeg VERSION 0.0.0)

set(CUSTOM_CMAKE_DIR "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_MODULE_PATH ${CUSTOM_CMAKE_DIR} ${CMAKE_MODULE_PATH})

find_package(Matlab REQUIRED COMPONENTS MX_LIBRARY)
find_package(FFMPEG REQUIRED)

add_subdirectory(ffmpeg)
add_subdirectory(matlab)
