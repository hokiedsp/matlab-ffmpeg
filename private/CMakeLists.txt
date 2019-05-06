# BUILD mex files

include_directories(..)

file(RELATIVE_PATH DstRelativePath ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

matlab_add_mex(NAME ffmpeginfo_mex SRC ffmpeginfo_mex.cpp LINK_TO $<TARGET_OBJECTS:ffmpeg>)
install(TARGETS ffmpeginfo_mex RUNTIME DESTINATION "${DstRelativePath}")

matlab_add_mex(NAME ffmpegformats_mex SRC ffmpegformats_mex.cpp LINK_TO $<TARGET_OBJECTS:ffmpeg>)
install(TARGETS ffmpegformats_mex RUNTIME DESTINATION "${DstRelativePath}")

matlab_add_mex(NAME ffmpegcodecs_mex SRC ffmpegcodecs_mex.cpp LINK_TO $<TARGET_OBJECTS:ffmpeg>)
install(TARGETS ffmpegcodecs_mex RUNTIME DESTINATION "${DstRelativePath}")

matlab_add_mex(NAME iscodec SRC iscodec.cpp LINK_TO $<TARGET_OBJECTS:ffmpeg>)
install(TARGETS iscodec RUNTIME DESTINATION "${DstRelativePath}")

if (WIN32 OR APPLE)
    # copy all the downloaded binary files as well
    file(GLOB BINFILES LIST_DIRECTORIES false "${FFMPEG_BIN_DIRECTORY}/bin/*")
    install(FILES ${BINFILES} DESTINATION ${DstRelativePath})
endif()

# copy all the m-files in the directory
file(GLOB MFILES LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.m")
install(FILES ${MFILES} DESTINATION ${DstRelativePath}) # copy all the package m-files
