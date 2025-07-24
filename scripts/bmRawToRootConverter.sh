#!/bin/bash

# Thin wrapper for bmRawToRootConverter executable, matching README usage

export SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source $SCRIPTS_DIR/init.sh

inputDirectory=""
outputDirectory=""
settingsFile=""
runNumber=""
calibFile=""
threshold=""
isCalib=false
skipEvent=false
cov=false
noCompile=false
cleanCompile=false

print_help() {
    echo "Usage:"
    echo "  $0 -j <settings.json> -r <RUN_NUMBER> [--is-calib] [--skip-event] [--cov] [-c <CALIB_FILE.root>] [-t <THRESHOLD>]"
    echo " --json-settings | -j <settings.json> : Path to the JSON settings file."
    echo " --run | -r <RUN_NUMBER> : Run number to process."
    echo " --calib-file | -c <CALIB_FILE.root> : Optional calibration"
    echo " --threshold | -t <THRESHOLD> : Optional threshold value."
    echo " --is-calib : Process as calibration data."
    echo " --skip-event : Skip event processing."
    echo " --cov : Enable covariance calculation."
    echo " --no-compile : Skip compilation of the code."
    echo " --clean-compile : Clean and recompile the code."
    echo " -h | --help : Print this help message."
    echo ""
    echo "Examples:"
    echo "  $0 -j config.json -r 21"
    echo "  $0 -j config.json -r 21 --is-calib --skip-event"
    echo "  $0 -j config.json -r 21 -c CALIB_FILE.root -t 25"
    exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -j|--json-settings) settingsFile="$2"; shift 2 ;;
        -r|--run) runNumber="$2"; shift 2 ;;
        -c) calibFile="$2"; shift 2 ;;
        -t) threshold="$2"; shift 2 ;;
        --is-calib) isCalib=true; shift ;;
        --skip-event) skipEvent=true; shift ;;
        --cov) cov=true; shift ;;
        --no-compile) noCompile=true; shift ;;
        --clean-compile) cleanCompile=true; shift ;;
        -h|--help) print_help ;;
        *) echo "Unknown option: $1"; print_help ;;
    esac
done

if [ -z "$settingsFile" ] || [ -z "$runNumber" ]; then
    echo "Both settings file (-j) and run number (-r) are required."
    print_help
fi

####################
# Find the settings file
echo "Looking for settings file $settingsFile. If execution stops, it means that the file was not found."
findSettings_command="$SCRIPTS_DIR/findSettings.sh -j $settingsFile"
# last line of the output of findSettings.sh is the full path of the settings file
settingsFile=$(. $findSettings_command | tail -n 1)
echo -e "Settings file found, full path is: $settingsFile \n"
####################

# Read input/output directories from JSON settings
inputDirectory=$(awk -F'"' '/inputDirectory/{print $4}' "$settingsFile")
outputDirectory=$(awk -F'"' '/outputDirectory/{print $4}' "$settingsFile")

# Find the input file in the input directory by run number (5-digit padded)
run_padded=$(printf "%05d" "$runNumber")
inputFile=$(find "$inputDirectory" -name "SCD_RUN${run_padded}_*.dat" | head -n 1)

if [ -z "$inputFile" ]; then
    echo "No input file found for run $runNumber in $inputDirectory."
    exit 1
fi

# Set output file name in output directory (same base name as input, but .root)
baseName=$(basename "$inputFile" .dat)
outputFile="$outputDirectory/${baseName}.root"

echo "Home directory: $HOME_DIR"

################################################
# Compile the code if requested
compile_command="$SCRIPTS_DIR/compile.sh -p $HOME_DIR --no-compile $noCompile --clean-compile $cleanCompile"
echo "Compiling the code with the following command:"
echo $compile_command
. $compile_command || exit

################################################
# Execute the code
cd $HOME_DIR/build || exit 1 # Ensure we are in the build directory

# Ensure output directory exists
mkdir -p "$outputDirectory"

# Set output file location (if the converter supports -o or similar, add it here)
# If not, the converter should write to the current directory, so cd there
cd "$outputDirectory"

# Use full path for input file
cmd="${HOME_DIR}/build/bmRawToRootConverter -i $inputFile"
if $isCalib; then
    cmd+=" --is-calib"
fi
if $skipEvent; then
    cmd+=" --skip-event"
fi
if $cov; then
    cmd+=" --cov"
fi
if [ -n "$calibFile" ]; then
    cmd+=" -c $calibFile"
fi
if [ -n "$threshold" ]; then
    cmd+=" -t $threshold"
fi

echo "Executing: $cmd"
$cmd

echo "Execution completed. Output file: $outputFile"