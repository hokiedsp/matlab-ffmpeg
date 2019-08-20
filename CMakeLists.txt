cmake_minimum_required(VERSION 3.14) 
# for FindMatlab support (may require a later version to detect the latest Matlab release)

project (matlab-ffmpeg)

# get matlab & mexutils
if(NOT EXISTS mexutils)
  execute_process(COMMAND git submodule update --init -- matlab-mexutils mexutils
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
set(MatlabMexutils_BuildExamples OFF)
add_subdirectory("mexutils")

# Set the default installation directory
if (NOT EXISTS ${MATLAB_USER_DIR})
  # if not found, show message and use the default CMake install directory
  message("Matlab user directory not found at ${MATLAB_USER_DIR}. It will be created if intallation directory not changed.")
elseif (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set (CMAKE_INSTALL_PREFIX "${MATLAB_USER_DIR}/ffmpeg" CACHE PATH "Installation directory" FORCE)
elseif (NOT IS_ABSOLUTE ${CMAKE_INSTALL_PREFIX})
  get_filename_component(NEW_CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" REALPATH BASE_DIR "${MATLAB_USER_DIR}")
  set (CMAKE_INSTALL_PREFIX "${NEW_CMAKE_INSTALL_PREFIX}" CACHE PATH "Installation directory" FORCE)
endif()
# set(MATLAB_FFMPEG_TOOLBOX_DIR "ffmpeg" CACHE PATH "Installation subdirectory for Matlab-FFmpeg package")
#set(MATLAB_FFMPEG_EXAMPLE_DIR "ffmpeg" CACHE PATH "Installation subdirectory for Matlab-FFmpeg examples")

# work the libffmpegio first
if(NOT EXISTS libffmpegio)
  execute_process(COMMAND git submodule update --init -- libffmpegio
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

set(LIBFFMPEGIO_SHARED OFF CACHE BOOL "True to build libffmpegio shared library")
set(LIBFFMPEGIO_STATIC ON CACHE BOOL "True to build libffmpegio static library")
set(LIBFFMPEGIO_INSTALL_FFMPEG ON CACHE BOOL "ON to install FFmpeg files (Windoes & OSX only)")
set(LIBFFMPEGIO_INSTALL_DEV OFF CACHE BOOL "ON to install lib & header files")
set(LIBFFMPEGIO_INSTALL_FFMPEG_BUILD_DEST "" CACHE STRING "Specify the install destination for FFMPEG build files")
# set(LIBFFMPEGIO_INSTALL_RUNTIME_DEST "" CACHE STRING "Specify the install destination for runtime files")
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
