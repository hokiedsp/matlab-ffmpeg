#
# Find the native FFMPEG includes and library
# This module defines
# FFMPEG_INCLUDE_DIR, where to find avcodec.h, avformat.h ...
# FFMPEG_LIBRARIES, the libraries to link against to use FFMPEG.
# FFMPEG_FOUND, If false, do not try to use FFMPEG.
# FFMPEG_ROOT, if this module use this path to find FFMPEG headers
# and libraries.

include(FindPackageHandleStandardArgs)

# ###################################
# Exploring the possible FFMPEG_ROOT
if(WIN32)
    if(NOT DEFINED FFMPEG_ROOT)
        #look in the system environmental variable named "FFMPEG_DIR"
        set(FFMPEG_ROOT $ENV{FFMPEG_DIR} CACHE PATH "FFMPEG installation root path")
    endif()
    if(DEFINED FFMPEG_ROOT AND NOT EXISTS ${FFMPEG_ROOT})
        # if ArrayFire_ROOT_DIR specified but erroneous
        message(WARNING "[FFMPEG] the specified path for FFMPEG_ROOT does not exist (${FFMPEG_ROOT})")
    endif()
else()
  # Linux & Mac should not need one
  set(FFMPEG_ROOT "" CACHE PATH "FFMPEG installation root path")
endif()

# Macro to find header and lib directories
# example: FFMPEG_FIND(AVFORMAT avformat avformat.h)
MACRO(FFMPEG_FIND varname shortname headername)
    string(TOUPPER ${shortname} LIB_FOUND)

    # old version of ffmpeg put header in $prefix/include/[ffmpeg]
    # so try to find header in include directory
    set(INC_DIR FFMPEG_${LIB_FOUND}_INCLUDE_DIR)
    FIND_PATH(${INC_DIR} lib${shortname}/${headername}
        PATHS
        ${FFMPEG_ROOT}/include/lib${shortname}
        ~/Library/Frameworks/lib${shortname}
        /Library/Frameworks/lib${shortname}
        /usr/local/include/lib${shortname}
        /usr/include/lib${shortname}
        /sw/include/lib${shortname} # Fink
        /opt/local/include/lib${shortname} # DarwinPorts
        /opt/csw/include/lib${shortname} # Blastwave
        /opt/include/lib${shortname}
        /usr/freeware/include/lib${shortname}
        PATH_SUFFIXES ffmpeg
        DOC "Location of FFMPEG Headers"
    )

    FIND_PATH(${INC_DIR} lib${shortname}/${headername}
        PATHS
        ${FFMPEG_ROOT}/include
        ~/Library/Frameworks
        /Library/Frameworks
        /usr/local/include
        /usr/include
        /sw/include # Fink
        /opt/local/include # DarwinPorts
        /opt/csw/include # Blastwave
        /opt/include
        /usr/freeware/include
        PATH_SUFFIXES ffmpeg
        DOC "Location of FFMPEG Headers"
    )

    set(LIB_NAME FFMPEG_${LIB_FOUND}_LIBRARY)
    FIND_LIBRARY(${LIB_NAME}
        NAMES ${shortname}
        PATHS
        ${FFMPEG_ROOT}/lib
        ~/Library/Frameworks
        /Library/Frameworks
        /usr/local/lib
        /usr/local/lib64
        /usr/lib
        /usr/lib64
        /sw/lib
        /opt/local/lib
        /opt/csw/lib
        /opt/lib
        /usr/freeware/lib64
        DOC "Location of FFMPEG Libraries"
    )

    set(LIB_FOUND FFMPEG_${LIB_FOUND}_FOUND)
    IF (${INC_DIR} AND ${LIB_NAME})
        SET(${LIB_FOUND} 1)
    ENDIF(${INC_DIR} AND ${LIB_NAME})

    list(APPEND _FFMPEG_required_variables ${INC_DIR} ${LIB_NAME} ${LIB_FOUND})

ENDMACRO(FFMPEG_FIND)

set(_FFMPEG_required_variables)
list(APPEND _FFMPEG_required_variables FFMPEG_ROOT)

# find stdint.h
IF(WIN32)

    FIND_PATH(FFMPEG_STDINT_INCLUDE_DIR stdint.h
        PATHS
        ${FFMPEG_ROOT}/include
        ~/Library/Frameworks
        /Library/Frameworks
        /usr/local/include
        /usr/include
        /sw/include # Fink
        /opt/local/include # DarwinPorts
        /opt/csw/include # Blastwave
        /opt/include
        /usr/freeware/include
        PATH_SUFFIXES ffmpeg
        DOC "Location of FFMPEG stdint.h Header"
    )

    IF (FFMPEG_STDINT_INCLUDE_DIR)
        SET(STDINT_OK TRUE)
    ENDIF()

    list(APPEND _FFMPEG_required_variables FFMPEG_STDINT_INCLUDE_DIR)

ELSE()

    SET(STDINT_OK TRUE)

ENDIF()

# must have format & codec
FFMPEG_FIND(LIBAVFORMAT avformat avformat.h)
FFMPEG_FIND(LIBAVCODEC  avcodec  avcodec.h)
list(APPEND _FFMPEG_required_variables FFMPEG_STDINT_INCLUDE_DIR)


# check for optional components
if(AVDEVICE IN_LIST FFMPEG_FIND_COMPONENTS)
    FFMPEG_FIND(LIBAVDEVICE avdevice avdevice.h)
endif()
if(AVFILTER IN_LIST FFMPEG_FIND_COMPONENTS)
    FFMPEG_FIND(LIBAVFILTER avfilter avfilter.h)
endif()
if(AVUTIL IN_LIST FFMPEG_FIND_COMPONENTS)
    FFMPEG_FIND(LIBAVUTIL   avutil   avutil.h)
endif()
if(SWSCALE IN_LIST FFMPEG_FIND_COMPONENTS)
    FFMPEG_FIND(LIBSWSCALE  swscale  swscale.h)  # not sure about the header to look for here.
endif()
if(SWRESAMPLE IN_LIST FFMPEG_FIND_COMPONENTS)
    FFMPEG_FIND(LIBSWRESAMPLE  swresample  swresample.h)  # not sure about the header to look for here.
endif()
if(POSTPROC IN_LIST FFMPEG_FIND_COMPONENTS)
    FFMPEG_FIND(LIBPOSTPROC  postproc  postproc.h)  # not sure about the header to look for here.
endif()
unset(_FFMPEG_find_component)

find_package_handle_standard_args(
  FFMPEG
  FOUND_VAR FFMPEG_FOUND
  REQUIRED_VARS ${_FFMPEG_required_variables}
  HANDLE_COMPONENTS)
unset(_FFMPEG_required_variables)

# Note we don't check FFMPEG_LIBSWSCALE_FOUND, FFMPEG_LIBAVDEVICE_FOUND,
# and FFMPEG_LIBAVUTIL_FOUND as they are optional.
IF (FFMPEG_FOUND)

    # all headers and library files are in the same folders
    SET(FFMPEG_INCLUDE_DIR ${FFMPEG_AVFORMAT_INCLUDE_DIR})
    
    # Note we don't add FFMPEG_LIBSWSCALE_LIBRARIES here,
    # it will be added if found later.
    SET(FFMPEG_LIBRARIES
        ${FFMPEG_AVFORMAT_LIBRARY}
        ${FFMPEG_AVDEVICE_LIBRARY}
        ${FFMPEG_AVCODEC_LIBRARY}
        ${FFMPEG_AVUTIL_LIBRARY}
        ${FFMPEG_AVFILTER_LIBRARY}
        ${FFMPEG_SWSCALE_LIBRARY}
        ${FFMPEG_SWRESAMPLE_LIBRARY}
        ${FFMPEG_POSTPROC_LIBRARY}
        )
ENDIF()

