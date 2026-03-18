#!/bin/bash

export SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source $SCRIPTS_DIR/init.sh

# Initialize variables
settingsFile=""
firstRun=""
lastRun=""

# Function to print help message
print_help() {
    echo "*****************************************************************************"
    echo "Usage: $0 -f <first_run> [-l <last_run>] -j <settings_file> [-h]"
    echo "  -f | --first-run      first run number"
    echo "  -l | --last-run       last run number"
    echo "  -j | --json-settings  json settings file"
    echo "  -h | --help           print this help message"
    echo "*****************************************************************************"
    exit 0
}

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
      -f|--first-run)      firstRun="$2"; shift 2 ;;
      -l|--last-run)       lastRun="$2"; shift 2 ;;
      -j|--json-settings)  settingsFile="$2"; shift 2 ;;
      -h|--help)           print_help ;;
      *)                   shift ;;
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

# Check if the user has selected a run number(s)
if [ -z "$firstRun" ]
then
    echo "Please select the first run number using -f <first_run>"
    exit 0
fi
if [ -z "$lastRun" ]
then
    echo "I have received only the number of the first run. Processing only run $firstRun"
    lastRun=$firstRun
fi

# Compile the code if needed
compile_command="$SCRIPTS_DIR/compile.sh -p $HOME_DIR --no-compile false --clean-compile false"
echo "Compiling the code with the following command:"
echo $compile_command
. $compile_command
if [ $? -ne 0 ]; then
    echo "Error: Compilation failed. Stopping execution."
    exit 1
fi

# Execute PAPERO_convert for each run
cd $HOME_DIR/build # executable is here

# Iterate over all the selected runs
for ((runit = $firstRun; runit <= $lastRun; runit++ ))
do
    echo "Processing run $runit"

    # Find the file
    runit_5d=$(printf "%05d" "$runit")
    filePath=$($SCRIPTS_DIR/findRun.sh -r "$runit" -i "$inputDirectory" | tail -n 1)
    
    # Stop execution if the selected run is not present in the input directory
    if [ -z "$filePath" ]
    then
        echo "Run $runit not found. Skipping."
        continue
    fi

    echo "Found file: $filePath"

    # in case it doesn't exist, creating output directory
    mkdir -p "$outputDirectory"

    fileName=$(basename "$filePath")
    fileName=${fileName%.*}
    calFile="${outputDirectory}/${fileName}.cal"
    rootFile="${outputDirectory}/${fileName}.root"

    if [ -f "$calFile" ]; then
        echo "Calibration file $calFile already exists. Skipping."
        continue
    fi

    # Run PAPERO_convert to convert .dat to ROOT
    papero_command="./PAPERO_convert ${filePath} ${rootFile} --dune"
    echo "Executing command: $papero_command"
    $papero_command
    if [ $? -ne 0 ]; then
        echo "Error: PAPERO_convert failed for run ${runit}."
        continue
    fi

    # Extract calibration from ROOT file
    extract_calibration="./calibration ${rootFile} --output ${outputDirectory}/${fileName} --dune --fast"
    echo "Executing command: $extract_calibration"
    $extract_calibration
    if [ $? -ne 0 ]; then
        echo "Error: calibration failed for run ${runit}."
        continue
    fi

    # Clean up intermediate ROOT file
    rm -f "$rootFile"

    echo "Calibration extracted for run $runit"
done

echo "Calibration extraction completed for all runs."