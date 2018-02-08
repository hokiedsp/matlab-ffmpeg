cmake_minimum_required(VERSION 3.3.0)
project(FFMPEG_TOOLBOX VERSION 0.0.0)

set(CUSTOM_CMAKE_DIR "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_MODULE_PATH ${CUSTOM_CMAKE_DIR} ${CMAKE_MODULE_PATH})

set(CMAKE_BUILD_TYPE "Release")

find_package(Matlab REQUIRED COMPONENTS MX_LIBRARY)
find_package(FFMPEG REQUIRED COMPONENTS AVFILTER AVUTIL SWSCALE SWRESAMPLE)

message("FFMPEG_INCLUDE_DIRS:" ${FFMPEG_INCLUDE_DIRS})

# should we use our own math functions

# first we can indicate the documentation build as an option and set it to ON by default
option(BUILD_DOC "Build documentation" ON)

# documentation build
# check if Doxygen is installed
find_package(Doxygen)
if (DOXYGEN_FOUND)
    # set input and output files
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/../docs/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # request to configure the file
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    message("Doxygen build started")

    # note the option ALL which allows to build the docs together with the application
    add_custom_target( doc_doxygen ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM )
else (DOXYGEN_FOUND)
  message("Doxygen need to be installed to generate the doxygen documentation")
endif (DOXYGEN_FOUND)

# project installation procedure
if (WIN32)
  set(MATLAB_USER_DIR "$ENV{USERPROFILE}/Documents/MATLAB")
else()
  set(MATLAB_USER_DIR "$ENV{home}/Documents/MATLAB/")
endif()

if (NOT EXISTS ${MATLAB_USER_DIR})
  message("Matlab user directory not found at ${MATLAB_USER_DIR}")
  message("   Cannot install.")
else()
  set(FFMPEG_TOOLBOX_INSTALL_DIR "${MATLAB_USER_DIR}/toolbox/ffmpeg_test")
  set(FFMPEG_TOOLBOX_TEST_INSTALL_DIR "${MATLAB_USER_DIR}/ffmpeg_test")

  set(FFMPEG_TOOLBOX_INSTALL_DIR ${FFMPEG_TOOLBOX_INSTALL_DIR} CACHE PATH "Matlab FFMPEG Toolbox Installation Directory")
  set(FFMPEG_TOOLBOX_TEST_INSTALL_DIR ${FFMPEG_TOOLBOX_TEST_INSTALL_DIR} CACHE PATH "Matlab FFMPEG Toolbox Test Scripts Directory")

  # add the install targets
  set(CMAKE_INSTALL_PREFIX ${FFMPEG_TOOLBOX_INSTALL_DIR})
  install(DIRECTORY matlab/ DESTINATION "." FILES_MATCHING PATTERN "*.m" PATTERN "include" EXCLUDE)
  install(DIRECTORY test/ DESTINATION "../../ffmpeg_test" FILES_MATCHING PATTERN "*.m")

endif()

# traverse subfolders
add_subdirectory(ffmpeg)
add_subdirectory(matlab)

