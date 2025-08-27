#!/bin/bash

# Script to compile the code, stopping in case of error

echo "Starting script $0"

# Initialize env variables
export SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source $SCRIPTS_DIR/init.sh

# Initialize variables
cleanCompile=false
noCompile=false
pwd=$PWD
nproc=$(nproc)
nproc_to_use=$((nproc-2))

# Function to print help message
print_help() {
    echo "*****************************************************************************"
    echo "Usage: ./compile.sh [-p <home_path>] [--clean-compile <true, false>] [--no-compile <true, false>] [-h]"
    echo "  -p | --home-path      path to the code home directory (/your/path/tof-reco, no / at the end). There is no default."
    echo "  -n | --no-compile          do not recompile the code. Default is to recompile the code"
    echo "  -j | --nproc            number of processors to use for compilation. Default is nproc-2"
    echo "  -c | --clean-compile       clean and recompile the code. Default is to recompile the code without cleaning"
    echo "  -h | --help           print this help message"
    echo "*****************************************************************************"
    exit 0
}

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
    -p|--home-path)         HOME_DIR="$2"; echo " Got home path as argument";    shift 2 ;;
    -n|--no-compile)        noCompile="${2:-true}";      shift ;;
    -j|--nproc)            nproc_to_use="${2:-$((nproc-2))}"; shift 2 ;;
    -c|--clean-compile)     cleanCompile="${2:-true}";   shift ;;
    -h|--help)              print_help ;;
    *)                      shift ;;
    esac
done


# if HOME_DIR is empty, print help message and exit
if [ -z "$HOME_DIR" ]; then
    echo "  Error: HOME_DIR is empty, meaning that you have not run from the repo and not given it as argument to the script."
    echo "  Please provide the path to the code home directory."
    print_help
fi

# if no compile, close the script
if [ "$noCompile" == true ]; then
    echo "  No compilation was requested. Stopping execution of this script."
    echo ""
else
    echo "  Compiling the code, these steps need to be run from build/"
    mkdir -p $BUILD_DIR || exit # in case it does not exist
    cd $BUILD_DIR || exit

    if [ "$cleanCompile" == true ]; then  
        echo "  Cleaning build folder..."
        rm -rfv $BUILD_DIR/*
        echo "  Done!"
    fi

    cmake ..
    # check if cmake was successful
    if [ $? -ne 0 ]
    then
        echo -e "  CMake failed. Stopping execution.\n"
        cd $pwd # go back to the original directory
        return 1
    fi

    make -j $nproc_to_use
    # check if make was successful
    if [ $? -ne 0 ]
    then
        echo "  Make failed. Stopping execution."
        echo ""
        cd $pwd # go back to the original directory
        exit 0
    fi
    
    cd $pwd # go back to the original directory
    echo "  Compilation successful!"
    echo ""

fi