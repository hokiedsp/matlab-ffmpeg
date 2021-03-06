# BUILD mex files

file(RELATIVE_PATH DstRelativePath ${PROJECT_SRC_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

matlab_add_mex(NAME ffmpeginfo_mex SRC ffmpeginfo_mex.cpp LINK_TO sharedlibs)
install(TARGETS ffmpeginfo_mex RUNTIME DESTINATION "${DstRelativePath}")

matlab_add_mex(NAME ffmpegformats_mex SRC ffmpegformats_mex.cpp LINK_TO sharedlibs)
install(TARGETS ffmpegformats_mex RUNTIME DESTINATION "${DstRelativePath}")

matlab_add_mex(NAME ffmpegcodecs_mex SRC ffmpegcodecs_mex.cpp LINK_TO sharedlibs)
install(TARGETS ffmpegcodecs_mex RUNTIME DESTINATION "${DstRelativePath}")

matlab_add_mex(NAME iscodec SRC iscodec.cpp LINK_TO sharedlibs)
install(TARGETS iscodec RUNTIME DESTINATION "${DstRelativePath}")

matlab_add_mex(NAME isframerate SRC isframerate.cpp LINK_TO sharedlibs)
install(TARGETS isframerate RUNTIME DESTINATION "${DstRelativePath}")

matlab_add_mex(NAME isframesize SRC isframesize.cpp LINK_TO sharedlibs)
install(TARGETS isframesize RUNTIME DESTINATION "${DstRelativePath}")

matlab_add_mex(NAME ispixfmt SRC ispixfmt.cpp LINK_TO sharedlibs)
install(TARGETS ispixfmt RUNTIME DESTINATION "${DstRelativePath}")

# copy all the m-files in the directory
file(GLOB MFILES LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.m")
install(FILES ${MFILES} DESTINATION ${DstRelativePath}) # copy all the package m-files
