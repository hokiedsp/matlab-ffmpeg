# compile back-end mex function for ffmpeg.VideoReader class
set(MEX_FILE "mex_backend")
set(MEX_FILE_NAME "mexReader.cpp")

matlab_add_mex(NAME ${MEX_FILE} SRC ${MEX_FILE_NAME} LINK_TO ${FFMPEG_LIBRARIES} $<TARGET_OBJECTS:${LIBFFMPEG}>)

file(RELATIVE_PATH DstRelativePath ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS ${MEX_FILE} RUNTIME DESTINATION "${DstRelativePath}")
