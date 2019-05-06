# compile back-end mex function for ffmpeg.VideoReader class
set(MEX_FILE "mex_backend")
set(MEX_FILE_NAME "mexVideoReader.cpp")

matlab_add_mex(NAME ${MEX_FILE} SRC ${MEX_FILE_NAME} LINK_TO ${FFMPEG_LIBRARIES})
include_directories(${FFMPEG_INCLUDE_DIR})
include_directories(${LIBFFMPEG_INCLUDE_DIR})
target_link_libraries(${MEX_FILE} $<TARGET_OBJECTS:${LIBFFMPEG}>) 

if(WIN32 AND MSVC)
  set_target_properties(${MEX_FILE} PROPERTIES LINK_FLAGS /EXPORT:mexFunction)
endif()

file(RELATIVE_PATH VideoReaderRelativePath "${CMAKE_SOURCE_DIR}/matlab" ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS ${MEX_FILE} RUNTIME DESTINATION "${FFMPEG_TOOLBOX_INSTALL_DIR}/${VideoReaderRelativePath}")

# display all the include directories
get_property(dirs TARGET ${MEX_FILE} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS "dir=\"${dir}\"")
endforeach()