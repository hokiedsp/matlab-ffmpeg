#
# Find the native FFMPEG includes and library
# This module defines
# FFMPEG_INCLUDE_DIR, where to find avcodec.h, avformat.h ...
# FFMPEG_LIBRARIES, the libraries to link against to use FFMPEG.
# FFMPEG_FOUND, If false, do not try to use FFMPEG.
# FFMPEG_ROOT, if this module use this path to find FFMPEG headers
# and libraries.

function (FFmpegDownloadDev VERSION)

if (FFMPEG_ROOT AND EXISTS ${FFMPEG_ROOT})
    message(STATUS "FFMPEG_ROOT already specified. Downloading the latest dev files halted.")
    return()
endif()

# only for Windows & OSX
if (NOT (WIN32 OR APPLE))
    return()
endif()

set(BASEURL "https://ffmpeg.zeranoe.com/builds/")

if (WIN32)
    set(OS "win64")
else()
    set(OS "macos64")
endif()

string(JOIN "-" DSTDIR "ffmpeg" ${VERSION} ${OS} "dev")
string(CONCAT DSTZIP ${CMAKE_CURRENT_BINARY_DIR} "/" ${DSTDIR} ".zip")
if (NOT EXISTS ${DSTZIP})
    string(JOIN "/" SRCURL ${BASEURL} ${OS} "dev" "${DSTDIR}.zip")
    file(DOWNLOAD ${SRCURL} ${DSTZIP} SHOW_PROGRESS)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf "${DSTZIP}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()

set(FFMPEG_ROOT "${CMAKE_CURRENT_BINARY_DIR}/${DSTDIR}" CACHE PATH "FFmpeg Dev Folder" FORCE)

string(JOIN "-" DSTDIR "ffmpeg" ${VERSION} ${OS} "shared")
string(CONCAT DSTZIP ${CMAKE_CURRENT_BINARY_DIR} "/" ${DSTDIR} ".zip")
if (NOT EXISTS ${DSTZIP})
    string(JOIN "/" SRCURL ${BASEURL} ${OS} "shared" "${DSTDIR}.zip")
    file(DOWNLOAD ${SRCURL} ${DSTZIP} SHOW_PROGRESS)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf "${DSTZIP}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()

set(FFMPEG_BIN_DIRECTORY  "${CMAKE_CURRENT_BINARY_DIR}/${DSTDIR}" CACHE PATH "FFmpeg Binary Folder" FORCE)

endfunction()
