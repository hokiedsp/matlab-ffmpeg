cmake_minimum_required(VERSION 3.14) 
# for FindMatlab support (may require a later version to detect the latest Matlab release)

project (matlab-ffmpeg)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/libffmpegio/cmake")

# get matlab & mexutils
if(NOT EXISTS mexutils)
  execute_process(COMMAND git submodule update --init -- matlab-mexutils mexutils
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
set(MatlabMexutils_BuildExamples OFF)
add_subdirectory("mexutils")

# Set the default installation directory
if (WIN32)
    file(TO_CMAKE_PATH $ENV{USERPROFILE} MATLAB_USER_DIR)
    set(MATLAB_USER_DIR "${MATLAB_USER_DIR}/Documents/MATLAB")
else()
  set(MATLAB_USER_DIR "$ENV{home}/Documents/MATLAB")
endif()
if (NOT EXISTS ${MATLAB_USER_DIR})
  # if not found, show message and use the default CMake install directory
  message("Matlab user directory not found at ${MATLAB_USER_DIR}. It will be created if intallation directory not changed.")
elseif (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set (CMAKE_INSTALL_PREFIX "${MATLAB_USER_DIR}/ffmpeg" CACHE PATH "Installation directory" FORCE)
endif()
# set(MATLAB_FFMPEG_TOOLBOX_DIR "ffmpeg" CACHE PATH "Installation subdirectory for Matlab-FFmpeg package")
#set(MATLAB_FFMPEG_EXAMPLE_DIR "ffmpeg" CACHE PATH "Installation subdirectory for Matlab-FFmpeg examples")

# work the libffmpegio first
if(NOT EXISTS libffmpegio)
  execute_process(COMMAND git submodule update --init -- libffmpegio
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

add_subdirectory("libffmpegio")
set(libffmpegio libffmpegio_static)

# Set C++ options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (FFMPEG_AVDEVICE_FOUND)
  add_compile_definitions(CONFIG_AVDEVICE)
endif()

# must build the ffmpeg library first as it is used by all mex files
add_subdirectory("src")
