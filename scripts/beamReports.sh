#!/bin/bash

export SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source $SCRIPTS_DIR/init.sh

# Initialize variables
settingsFile=""
noCompile=false
cleanCompile=false
verbose=false
debug=false

# Function to print help message
print_help() {
    echo "*****************************************************************************"
    echo "Usage: $0 -j <settings_file> [-h]"
    echo "  -p | --home-path      path to the code home directory. Default is current directory."
    echo "  -j | --json-settings  json settings file to run this app"
    echo "  --no-compile          do not recompile the code. Default is to recompile the code"
    echo "  --clean-compile       clean and recompile the code. Default is to recompile the code without cleaning"
    echo "  -v | --verbose        run in verbose mode"
    echo "  -d | --debug          run in debug mode"
    echo "  -h | --help           print this help message"
    echo "*****************************************************************************"
    exit 0
}

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
      -p|--home-path)      HOME_DIR="$2"; shift 2 ;;
      -j|--json-settings)  settingsFile="$2"; shift 2 ;;
      --no-compile)        noCompile=true; shift ;;
      --clean-compile)     cleanCompile=true; shift ;;
      -v|--verbose)        verbose=true; shift ;;
      -d|--debug)          debug=true; shift ;;
      -h|--help)           print_help ;;
      *)                   echo "Unknown option: $1"; print_help ;;
    esac
done

if [ -z "$settingsFile" ]
then
  echo "Please specify the settings file, using flag -j <settings_file>. Stopping execution."
  exit 0
fi

####################
# Find the settings file
echo "Looking for settings file $settingsFile. If execution stops, it means that the file was not found."
findSettings_command="$SCRIPTS_DIR/findSettings.sh -j $settingsFile"
# last line of the output of findSettings.sh is the full path of the settings file
settingsFile=$(. $findSettings_command | tail -n 1)
echo -e "Settings file found, full path is: $settingsFile \n"

echo "Home is ${HOME_DIR}."
echo "Currently in $(pwd), moving to ${HOME_DIR} to run the script."
cd $HOME_DIR

# read from settings file the input and output paths
inputDirectory=$(awk -F'"' '/inputDirectory/{print $4}' "$settingsFile")
outputDirectory=$(awk -F'"' '/outputDirectory/{print $4}' "$settingsFile")
verbose=$(awk -F'"' '/verboseMode/{print $4}' "$settingsFile")
debug=$(awk -F'"' '/debugMode/{print $4}' "$settingsFile")

echo "Input directory: $inputDirectory"
echo "Output directory: $outputDirectory"

# Compile the code if requested
compile_command="$SCRIPTS_DIR/compile.sh -p $HOME_DIR --no-compile $noCompile --clean-compile $cleanCompile"
echo "Compiling the code with the following command:"
echo $compile_command
. $compile_command
if [ $? -ne 0 ]; then
    echo "Error: Compilation failed. Stopping execution."
    exit 1
fi

cd $HOME_DIR/build

# Collect available BEAM clusters.root files in outputDirectory (skip CAL)
clusterFiles=$(find "$outputDirectory" -name "*_BEAM_*_clusters.root" -type f)

# If no BEAM cluster files found, fall back to any clusters.root (including CAL) so the script can still run
if [ -z "$clusterFiles" ]; then
    echo "No BEAM *_clusters.root files found in $outputDirectory. Falling back to any *_clusters.root files."
    clusterFiles=$(find "$outputDirectory" -name "*_clusters.root" -type f)
    if [ -z "$clusterFiles" ]; then
        echo "No clusters.root files found in $outputDirectory"
        exit 1
    fi
fi

echo "Found clusters.root files:"
echo "$clusterFiles"
echo ""

beam_settings_file="$HOME_DIR/parameters/beam_settings.dat"

# Build groupBeam command as an array to preserve arguments and avoid embedding literal quotes
# GroupBeam should read formatted cluster ROOT files in the output directory
groupBeam_cmd=("./groupBeam" -i "$outputDirectory" -o "$outputDirectory" -b "$beam_settings_file")

if [ "$verbose" = true ]; then
    groupBeam_cmd+=( -v )
fi
if [ "$debug" = true ]; then
    groupBeam_cmd+=( -d )
fi

echo "Executing: ${groupBeam_cmd[*]}"
"${groupBeam_cmd[@]}"
if [ $? -ne 0 ]; then
    echo "Error: groupBeam failed."
    exit 1
fi

# Process grouped beam files produced by groupBeam - generate reports in SPS mode (beam-condition reports)
groupedFiles=$(find "$outputDirectory" -maxdepth 1 -type f -name "*GeV.root" -print)

if [ -z "$groupedFiles" ]; then
    echo "No grouped beam files (/*GeV.root) found in $outputDirectory. Nothing to run in SPS mode."
else
    echo "Found grouped beam files (by condition):"
    echo "$groupedFiles"
    echo ""

    for groupedFile in $groupedFiles; do
        echo "Processing grouped file: $(basename "$groupedFile")"

        # Run runReport from project root so it can access parameters/beam_settings.dat
        runReport_cmd=("$BUILD_DIR/runReport" -i "$groupedFile" -o "$outputDirectory" --sps-run)

        if [ "$verbose" = true ]; then
            runReport_cmd+=( -v )
        fi
        if [ "$debug" = true ]; then
            runReport_cmd+=( -d )
        fi

        echo "Executing: (cd \"$HOME_DIR\" && ${runReport_cmd[*]})"
        (cd "$HOME_DIR" && "${runReport_cmd[@]}")
        if [ $? -ne 0 ]; then
            echo "Error: Report generation failed for grouped file $groupedFile"
            continue
        fi

        echo "Successfully generated SPS-mode report for $(basename "$groupedFile")"
        echo ""
    done

    echo "All beam-condition (SPS-mode) reports have been generated in: $outputDirectory"
fi