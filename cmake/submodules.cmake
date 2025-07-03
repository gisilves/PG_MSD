###################
# link submodules
###################

cmessage( STATUS "Adding submodules to the project..." )
set( SUBMODULE_DIR ${CMAKE_SOURCE_DIR}/submodules )

# Function to make sure if a given submodule is present:
function( checkSubmodule )
  list( GET ARGV 0 SELECTED_SUBMODULE )
  cmessage( WARNING "Checking submodule: ${SELECTED_SUBMODULE}" )

  file( GLOB FILES_IN_DIRECTORY "${SUBMODULE_DIR}/${SELECTED_SUBMODULE}/*")

  if( FILES_IN_DIRECTORY )
    cmessage( STATUS "Git submodule ${SELECTED_SUBMODULE} is present" )
  else()
    cmessage( ERROR "Git submodule ${SELECTED_SUBMODULE} is not present, please checkout: \"git submodule update --init --remote --recursive\"" )
    cmessage( FATAL_ERROR "CMake fatal error." )
  endif()
endfunction( checkSubmodule )


###################################
# Simple C++ logger
###################################
cmessage( STATUS "Looking for simple-cpp-logger submodule..." )
checkSubmodule( simple-cpp-logger )
include_directories( ${SUBMODULE_DIR}/simple-cpp-logger/include )
add_definitions( -D LOGGER_MAX_LOG_LEVEL_PRINTED=6 )
add_definitions( -D LOGGER_PREFIX_LEVEL=3 )
add_definitions( -D LOGGER_TIME_FORMAT="\\\"%Y.%m.%d %H:%M:%S"\\\" )
add_definitions( -D LOGGER_ENABLE_COLORS=1 )
add_definitions( -D LOGGER_ENABLE_COLORS_ON_USER_HEADER=1 )
add_definitions( -D LOGGER_PREFIX_FORMAT="\\\"{TIME} {SEVERITY} {FILENAME}"\\\" )


###################################
# Simple C++ cmd line parser
###################################
cmessage( STATUS "Looking for simple-cpp-cmd-line-parser submodule..." )
checkSubmodule( simple-cpp-cmd-line-parser )
include_directories( ${SUBMODULE_DIR}/simple-cpp-cmd-line-parser/include )


###################################
# C++ generic toolbox
###################################
cmessage( STATUS "Looking for cpp-generic-toolbox submodule..." )
checkSubmodule( cpp-generic-toolbox )
include_directories( ${SUBMODULE_DIR}/cpp-generic-toolbox/include )
add_definitions( -D PROGRESS_BAR_FILL_TAG="\\\"DUNE\#"\\\" )
add_definitions( -D PROGRESS_BAR_ENABLE_RAINBOW=1 )
