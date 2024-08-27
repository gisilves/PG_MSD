# Detecting compiler
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" )
  message( STATUS "Detected GCC version: ${CMAKE_CXX_COMPILER_VERSION}" )
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8.5)
    message( FATAL_ERROR "GCC version must be at least 5.0" )
  endif()
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang" )
  message( STATUS "Detected Clang version: ${CMAKE_CXX_COMPILER_VERSION}" )
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.3)
    message( FATAL_ERROR "Clang version must be at least 3.3" )
  endif()
else()
  message( WARNING "You are using an untested compiler." )
endif()

if( WITH_ROOT )
  cmessage( STATUS "Looking for ROOT install..." )

  # find ROOT package
  find_package( ROOT COMPONENTS RIO Net )
  if( ROOT_FOUND )
    cmessage( STATUS "[ROOT]: ROOT found." )

    cmessage( STATUS "[ROOT]: ROOT cmake use file ${ROOT_USE_FILE}")
    cmessage( STATUS "[ROOT]: ROOT include directory: ${ROOT_INCLUDE_DIRS}" )
    cmessage( STATUS "[ROOT]: ROOT C++ Flags: ${ROOT_CXX_FLAGS}" )

    # Grab functions such as generate dictionary
    include( ${ROOT_USE_FILE} )

    #  if (NOT ROOT_minuit2_FOUND)
    # Minuit2 wasn't found, but make really sure before giving up.
    #    execute_process (
    #        COMMAND root-config --has-minuit2
    #        OUTPUT_VARIABLE ROOT_minuit2_FOUND
    #        OUTPUT_STRIP_TRAILING_WHITESPACE
    #    )
    #  endif(NOT ROOT_minuit2_FOUND)

    # inc dir is $ROOTSYS/include/root
    set(CMAKE_ROOTSYS ${ROOT_INCLUDE_DIRS}/..)

  else( ROOT_FOUND )
    cmessage( STATUS "find_package didn't find ROOT. Using shell instead...")

    # ROOT
    if(NOT DEFINED ENV{ROOTSYS} )
      cmessage( FATAL_ERROR "$ROOTSYS is not defined, please set up root first." )
    else()
      cmessage( STATUS "Using ROOT installed at $ENV{ROOTSYS}")
      set(CMAKE_ROOTSYS $ENV{ROOTSYS})
    endif()

    cmessage( STATUS "Including local GENERATE_ROOT_DICTIONARY implementation." )
#    include(${CMAKE_SOURCE_DIR}/cmake/utils/GenROOTDictionary.cmake)
    execute_process(COMMAND root-config --cflags
        OUTPUT_VARIABLE ROOT_CXX_FLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND root-config --libs
        OUTPUT_VARIABLE ROOT_LIBRARIES
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND root-config --version
        OUTPUT_VARIABLE ROOT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process (COMMAND root-config --ldflags
        OUTPUT_VARIABLE ROOT_LINK_FLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process (COMMAND root-config --has-minuit2
        OUTPUT_VARIABLE ROOT_minuit2_FOUND
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    cmessage( STATUS "[ROOT]: root-config --version: ${ROOT_VERSION}")
    cmessage( STATUS "[ROOT]: root-config --libs: ${ROOT_LIBRARIES}")
    cmessage( STATUS "[ROOT]: root-config --cflags: ${ROOT_CXX_FLAGS}")
    cmessage( STATUS "[ROOT]: root-config --ldflags: ${ROOT_LINK_FLAGS}")

    add_compile_options("SHELL:${ROOT_CXX_FLAGS}")
    #  add_link_options("SHELL:${ROOT_LINK_FLAGS}")

  endif( ROOT_FOUND )

  # Try to figure out which version of C++ was used to compile ROOT.  ROOT
  # generates header files that depend on the compiler version so we will
  # need to use the same version.
  execute_process(COMMAND root-config --has-cxx14 COMMAND grep yes
      OUTPUT_VARIABLE ROOT_cxx14_FOUND
      OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND root-config --has-cxx17 COMMAND grep yes
      OUTPUT_VARIABLE ROOT_cxx17_FOUND
      OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND root-config --has-cxx20 COMMAND grep yes
      OUTPUT_VARIABLE ROOT_cxx20_FOUND
      OUTPUT_STRIP_TRAILING_WHITESPACE)

  # Explicitly set the compiler version so that it will match the
  # compiler that was used to compile ROOT.  Recent ROOT documentation
  # explicitly notes that the appliation needs to use the same C++
  # standard as ROOT.
  if( ROOT_cxx14_FOUND )
    message(STATUS "ROOT compiled with C++14")
    set(CMAKE_CXX_STANDARD 14)
  elseif( ROOT_cxx17_FOUND )
    message(STATUS "ROOT compiled with C++17")
    set(CMAKE_CXX_STANDARD 17)
  elseif( ROOT_cxx20_FOUND )
    message(STATUS "ROOT compiled with C++20")
    set(CMAKE_CXX_STANDARD 20)
  else()
    message(WARNING "ROOT C++ standard not set, use ROOT minimum (C++14)")
    set(CMAKE_CXX_STANDARD 14)
  endif()

  # If GNU, enable generation of ROOT dictionary
  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" )
    option (BUILD_ROOT_DICTIONARY "Build ROOT dictionary" ON)
  endif()

else ()
  cmessage( STATUS "ROOT-less build selected." )
  set(CMAKE_CXX_STANDARD 17)
endif ()

# JSON
# NLOHMANN JSON
find_package( nlohmann_json )

find_path(NLOHMANN_JSON_INCLUDE_DIR NAMES nlohmann/json.hpp)
if (nlohmann_json_FOUND)
  cmessage( STATUS "nlohmann JSON library found: ${NLOHMANN_JSON_INCLUDE_DIR}")
  # Additional actions for when the library is found
else()
  if (NLOHMANN_JSON_INCLUDE_DIR)
    cmessage( STATUS "nlohmann JSON header found: ${NLOHMANN_JSON_INCLUDE_DIR}/nlohmann/json.hpp")
    # Additional actions for when the library is found
  else()
    cmessage( FATAL_ERROR "nlohmann JSON library not found")
    # Additional actions for when the library is not found
  endif()
endif()
include_directories( ${NLOHMANN_JSON_INCLUDE_DIR} )


