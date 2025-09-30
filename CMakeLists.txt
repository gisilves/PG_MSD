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

cmessage( STATUS "Creating flat_convert app..." )
add_executable( flat_convert ${CMAKE_CURRENT_SOURCE_DIR}/src/flat_convert.cpp)
target_include_directories( flat_convert PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( flat_convert ${OCA_LIBS} )
install( TARGETS flat_convert DESTINATION bin )

cmessage( STATUS "Creating clustering app..." )
add_executable( clustering ${CMAKE_CURRENT_SOURCE_DIR}/src/clustering.cpp)
target_include_directories( clustering PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( clustering ${OCA_LIBS} )
install( TARGETS clustering DESTINATION bin )

cmessage( STATUS "Creating runReport app..." )
add_executable( runReport ${CMAKE_CURRENT_SOURCE_DIR}/src/runReport.cpp)
target_include_directories( runReport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( runReport ${OCA_LIBS} )
install( TARGETS runReport DESTINATION bin )

cmessage( STATUS "Creating calibration app..." )
add_executable( calibration ${CMAKE_CURRENT_SOURCE_DIR}/src/calibration.cpp)
target_include_directories( calibration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( calibration ${OCA_LIBS} )
install( TARGETS calibration DESTINATION bin )

cmessage( STATUS "Creating formatting app..." )
add_executable( formatting ${CMAKE_CURRENT_SOURCE_DIR}/src/formatting.cpp)
target_include_directories( formatting PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( formatting ${OCA_LIBS} )
install( TARGETS formatting DESTINATION bin )

cmessage( STATUS "Creating event_display app..." )
add_executable( event_display ${CMAKE_CURRENT_SOURCE_DIR}/src/event_display.cpp)
target_include_directories( event_display PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( event_display ${OCA_LIBS} )
install( TARGETS event_display DESTINATION bin )

cmessage( STATUS "Creating hits_vs_sigma app..." )
add_executable( hits_vs_sigma ${CMAKE_CURRENT_SOURCE_DIR}/src/hits_vs_sigma.cpp)
target_include_directories( hits_vs_sigma PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( hits_vs_sigma ${OCA_LIBS} )
install( TARGETS hits_vs_sigma DESTINATION bin )

cmessage( STATUS "Creating bmRawToRootConverter app..." )
add_executable( bmRawToRootConverter ${CMAKE_CURRENT_SOURCE_DIR}/src/bmRawToRootConverter.cxx)
target_include_directories( bmRawToRootConverter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( bmRawToRootConverter ${OCA_LIBS} )
install( TARGETS bmRawToRootConverter DESTINATION bin )

cmessage( STATUS "Creating groupBeam app..." )
add_executable( groupBeam ${CMAKE_CURRENT_SOURCE_DIR}/src/groupBeam.cpp)
target_include_directories( groupBeam PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
target_link_libraries( groupBeam ${OCA_LIBS} )
install( TARGETS groupBeam DESTINATION bin )

###############################################################3


# this does not work yet
# add_executable( raw_viewer ${CMAKE_CURRENT_SOURCE_DIR}/src/viewerGUI.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/event.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/guiDict.cpp)
# target_include_directories( raw_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc )
# target_link_libraries( raw_viewer ${ROOT_LIBRARIES} )
# install( TARGETS raw_viewer DESTINATION bin )