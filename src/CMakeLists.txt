project (matlab-ffmpeg)

# must build the ffmpeg library first as it is used by all mex files
add_subdirectory("ffmpeg")
add_subdirectory("sandbox")

# build visible mex functions
matlab_add_mex(NAME ffmpegmediatypes SRC ffmpegmediatypes.cpp LINK_TO $<TARGET_OBJECTS:ffmpeg>)
install(TARGETS ffmpegmediatypes RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})

matlab_add_mex(NAME ffmpegget SRC ffmpegget.cpp LINK_TO $<TARGET_OBJECTS:ffmpeg>)
install(TARGETS ffmpegget RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})

matlab_add_mex(NAME ffmpegcolors SRC ffmpegcolors.cpp LINK_TO $<TARGET_OBJECTS:ffmpeg>)
install(TARGETS ffmpegcolors RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})

matlab_add_mex(NAME ffmpegpixfmts SRC ffmpegpixfmts.cpp LINK_TO $<TARGET_OBJECTS:ffmpeg>)
install(TARGETS ffmpegpixfmts RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})

# install copies all the m-files
# add the install procedures
file(GLOB MFILES LIST_DIRECTORIES false RELATIVE ${PROJECT_SOURCE_DIR} "*.m")
install(FILES ${MFILES} DESTINATION ${CMAKE_INSTALL_PREFIX}) # copy all the package m-files

# copy private m-files & build private mex functions
add_subdirectory("private")

# copy ffmpegfilter m-file package
add_subdirectory("+ffmpegfilter")