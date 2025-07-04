cmake_minimum_required(VERSION 3.17)
project(beam-monitor-analysis)

# using ROOT
option ( WITH_ROOT "Using ROOT" ON )

include( ${CMAKE_SOURCE_DIR}/cmake/cmessage.cmake )
include( ${CMAKE_SOURCE_DIR}/cmake/dependencies.cmake )
include( ${CMAKE_SOURCE_DIR}/cmake/submodules.cmake )

###############################################################3

# Libraries 

set( OCA_LIBS "ocaAnaLibs" )

set( SRC_FILES
    # ${CMAKE_CURRENT_SOURCE_DIR}/src/calibration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/anyoption.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ocaEvent.cpp
)

add_library( ${OCA_LIBS} STATIC ${SRC_FILES} )
target_include_directories(
    ${OCA_LIBS} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)
target_link_libraries( ${OCA_LIBS} PUBLIC  ${ROOT_LIBRARIES} )
install( TARGETS ${OCA_LIBS} DESTINATION lib )

###############################################################3

cmessage( STATUS "Creating PAPERO_convert app..." )
add_executable( PAPERO_convert ${CMAKE_CURRENT_SOURCE_DIR}/src/PAPERO_convert.cpp)
target_include_directories( PAPERO_convert PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( PAPERO_convert ${OCA_LIBS} )
install( TARGETS PAPERO_convert DESTINATION bin )

cmessage( STATUS "Creating calibration app..." )
add_executable( calibration ${CMAKE_CURRENT_SOURCE_DIR}/src/calibration.cpp)
target_include_directories( calibration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( calibration ${OCA_LIBS} )
install( TARGETS calibration DESTINATION bin )

cmessage( STATUS "Creating dataAnalyzer app..." )
add_executable( dataAnalyzer ${CMAKE_CURRENT_SOURCE_DIR}/src/dataAnalyzer.cpp)
target_include_directories( dataAnalyzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( dataAnalyzer ${OCA_LIBS} )


cmessage( STATUS "Creating bmRawToRootConverter app..." )
add_executable( bmRawToRootConverter ${CMAKE_CURRENT_SOURCE_DIR}/src/bmRawToRootConverter.cxx)
target_include_directories( bmRawToRootConverter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( bmRawToRootConverter ${OCA_LIBS} )
install( TARGETS bmRawToRootConverter DESTINATION bin )

cmessage( STATUS "Creating bmConvertToFlatTree app..." )
add_executable( bmConvertToFlatTree ${CMAKE_CURRENT_SOURCE_DIR}/src/bmConvertToFlatTree.cpp)
target_include_directories( bmConvertToFlatTree PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( bmConvertToFlatTree ${OCA_LIBS} )
install( TARGETS bmConvertToFlatTree DESTINATION bin )

###############################################################3


# this does not work yet
# add_executable( raw_viewer ${CMAKE_CURRENT_SOURCE_DIR}/src/viewerGUI.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/event.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/guiDict.cpp)
# target_include_directories( raw_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
# target_link_libraries( raw_viewer ${ROOT_LIBRARIES} )
# install( TARGETS raw_viewer DESTINATION bin )