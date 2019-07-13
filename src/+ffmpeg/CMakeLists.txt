matlab_add_mex(NAME audioinfo SRC audioinfo.cpp LINK_TO ${FFMPEG_LIBRARIES} $<TARGET_OBJECTS:${LIBFFMPEG}>)

file(RELATIVE_PATH DstRelativePath ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS ${MEX_FILE} RUNTIME DESTINATION "${DstRelativePath}")

# copy all the m-files in the directory
file(GLOB MFILES LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.m")
install(FILES ${MFILES} DESTINATION ${DstRelativePath}) # copy all the package m-files


add_subdirectory(@Reader)
