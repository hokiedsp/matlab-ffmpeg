# BUILD mex files

file(RELATIVE_PATH DstRelativePath ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

# copy all the m-files in the directory
file(GLOB MFILES LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.m")
install(FILES ${MFILES} DESTINATION ${DstRelativePath}) # copy all the package m-files
