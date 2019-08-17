matlab_add_mex(NAME audioinfo SRC audioinfo.cpp LINK_TO sharedlibs)

file(RELATIVE_PATH DstRelativePath ${PROJECT_SRC_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS audioinfo RUNTIME DESTINATION "${DstRelativePath}")

matlab_add_mex(NAME audioread SRC audioread.cpp LINK_TO sharedlibs)

file(RELATIVE_PATH DstRelativePath ${PROJECT_SRC_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS audioread RUNTIME DESTINATION "${DstRelativePath}")

# copy all the m-files in the directory
file(GLOB MFILES LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.m")
install(FILES ${MFILES} DESTINATION ${DstRelativePath}) # copy all the package m-files


add_subdirectory(@Reader)
