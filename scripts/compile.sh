#!/bin/bash

# Compile the code, stopping in case of error

# TODO add detection of home location
REPO_HOME=$(git rev-parse --show-toplevel) # does not have / at the end. 
cd $REPO_HOME/build

echo "Compiling the code, needs to be run from build/"


# Parse options
cleanCompile=false

# Function to print help message
print_help() {
    echo "*****************************************************************************"
    echo "Usage: ./createOutputTree.sh -f <first_run> [-l <last_run>] -j <settings_file> [-h]"
    echo "  -p | --home-path      path to the code home directory (/your/path/tof-reco, no / at the end). There is no default."
    echo "  -j | --json-settings  json settings file to run this app"
    echo "  --clean-compile       clean and recompile the code. Default is to recompile the code without cleaning"
    echo "  --source-lxplus       source the lxplus environment. Default is to not source the lxplus environment"
    echo "  -h | --help           print this help message"
    echo "*****************************************************************************"
    exit 0
}

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
      -p|--home-path)
        REPO_HOME="$2"
        shift 2
        ;;
      -j|--json-settings)
        settingsFile="$2"
        shift 2
        ;;
      --clean-compile)
        cleanCompile=true
        shift
        ;;
      --source-lxplus)
        sourceLxplus=true
        shift
        ;;
      -h|--help)
        print_help
        ;;
      *)
        shift
        ;;
    esac
done

if [ "$cleanCompile" = true ]; then  
    echo "Cleaning build folder..."
    rm -r $REPO_HOME/build/*
    echo "Done!"
fi

cmake ..
# check if cmake was successful
if [ $? -ne 0 ]
then
    echo "CMake failed. Stopping execution."
    exit 0
fi
nproc=$(nproc)

make -j $nproc
# check if make was successful
if [ $? -ne 0 ]
then
    echo "Make failed. Stopping execution."
    exit 0
fi