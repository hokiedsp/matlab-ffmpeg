cmake_minimum_required(VERSION 3.14) 
# for FindMatlab support (may require a later version to detect the latest Matlab release)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(FFMPEG_VERSION "latest" CACHE STRING "FFmpeg version (only relevant for Win32 and Apple)")

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

# download FFmpeg DEV & SHARED
include("${CMAKE_CURRENT_LIST_DIR}/cmake/FFmpegDownloadDev.cmake")
FFmpegDownloadDev(${FFMPEG_VERSION})

# Find dependent packages
find_package(MATLAB REQUIRED MX_LIBRARY)
find_package(FFMPEG REQUIRED COMPONENTS AVUTIL OPTIONAL_COMPONENTS AVDEVICE SWSCALE)

# Set C++ options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (FFMPEG_AVDEVICE_FOUND)
  add_compile_definitions(CONFIG_AVDEVICE)
endif()

get_filename_component(Matlab_RELEASE ${Matlab_ROOT_DIR} NAME)
matlab_get_version_from_release_name(${Matlab_RELEASE} Matlab_VERSION)
if (Matlab_VERSION AND Matlab_VERSION VERSION_LESS "8.5")
  add_compile_definitions(MATLAB_PRE_R2015A)
endif()

# turn off new Matlab class support
set(Matlab_HAS_CPP_API 0)

# common include & libraries
include_directories(${FFMPEG_INCLUDE_DIR} ${Matlab_INCLUDE_DIRS} "mexutils/include")
link_libraries(${FFMPEG_LIBRARIES})

# must build the ffmpeg library first as it is used by all mex files
add_subdirectory("src")
