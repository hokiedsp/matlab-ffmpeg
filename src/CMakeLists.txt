set(PROJECT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# must build the ffmpeg library first as it is used by all mex files
add_library(ffmpeg-utils OBJECT)
target_sources(ffmpeg-utils PRIVATE ../libffmpegio/src/ffmpegException.cpp)
target_link_libraries(ffmpeg-utils PRIVATE libmex ${libffmpegio})
add_subdirectory("utils") # creates LIBFFMPEG target variable

# use this interface library for shared resources
add_library(sharedlibs INTERFACE)
target_link_libraries(sharedlibs INTERFACE $<TARGET_OBJECTS:ffmpeg-utils> ${libffmpegio} libmexutils)
target_include_directories(sharedlibs INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/utils)

# build visible mex functions
matlab_add_mex(NAME ffmpegmediatypes SRC ffmpegmediatypes.cpp LINK_TO sharedlibs)
install(TARGETS ffmpegmediatypes RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})

matlab_add_mex(NAME ffmpegget SRC ffmpegget.cpp LINK_TO sharedlibs)
install(TARGETS ffmpegget RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})

matlab_add_mex(NAME ffmpegcolors SRC ffmpegcolors.cpp LINK_TO sharedlibs)
install(TARGETS ffmpegcolors RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})

matlab_add_mex(NAME ffmpegpixfmts SRC ffmpegpixfmts.cpp LINK_TO sharedlibs)
install(TARGETS ffmpegpixfmts RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})

# install copies all the m-files
# add the install procedures
file(GLOB MFILES LIST_DIRECTORIES false RELATIVE ${PROJECT_SRC_DIR} "*.m")
install(FILES ${MFILES} DESTINATION ${CMAKE_INSTALL_PREFIX}) # copy all the package m-files

# copy private m-files & build private mex functions
add_subdirectory("private")

# copy ffmpegfilter m-file package
add_subdirectory("+ffmpegfilter")
# add_subdirectory("sandbox")
add_subdirectory("+ffmpeg")
