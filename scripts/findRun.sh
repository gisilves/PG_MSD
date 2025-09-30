#!/bin/bash

# The purpose of this script is to give an interface to search for a DAQ file
# Current implementation targets DUNE BM naming: SCD_RUN<5d>_*.dat
# It can be run standalone or from other scripts; the LAST LINE of output lists the full path(s).

export SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPTS_DIR/init.sh"

# defaults
software="linux"
inputFolder=""
settingsFile=""
runNumber=""
subrun="all" # unused for current naming, kept for compatibility
runType="all" # unused here

print_help() {
  echo "*****************************************************************************"
  echo "Usage: $0 -r <run_number> [-i <input_folder>] [-j <settings_file>] [-w <linux|windows>] [--subrun <n>] [-h]"
  echo " -r | --run-number     Run number (integer)"
  echo " -i | --input-folder   Folder to search (recursively). If omitted, read from settings file"
  echo " -j | --json-settings  JSON settings with inputDirectory field"
  echo " -w | --which-software linux (default) or windows (reserved)"
  echo " --subrun              Subrun (unused for current pattern; kept for compat)"
  echo "*****************************************************************************"
}

# Parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    -r|--run-number) runNumber="$2"; shift 2 ;;
    --subrun)        subrun="$2"; shift 2 ;;
    -i|--input-folder) inputFolder="$2"; shift 2 ;;
    -j|--json-settings) settingsFile="$2"; shift 2 ;;
    -w|--which-software) software="$2"; shift 2 ;;
    -h|--help) print_help; exit 0 ;;
    *) shift ;;
  esac
done

if [ -z "$runNumber" ]; then
  echo "Please specify -r <run_number>"; echo ""; exit 1
fi

if [ -z "$inputFolder" ]; then
  if [ -n "$settingsFile" ]; then
    echo "Reading inputDirectory from settings: $settingsFile"
    inputFolder=$(awk -F '"' '/inputDirectory/{print $4}' "$settingsFile")
  fi
fi

if [ -z "$inputFolder" ]; then
  echo "Input folder not provided and not found in settings."; echo ""; exit 1
fi

if [ ! -d "$inputFolder" ]; then
  echo "Input folder $inputFolder does not exist."; echo ""; exit 1
fi

if [ "$software" != "linux" ]; then
  echo "Only linux pattern supported currently."; echo ""; exit 1
fi

runit_5d=$(printf "%05d" "$runNumber")
# search recursively; restrict to .dat
runsPaths=$(find "$inputFolder" -type f -name "SCD_RUN${runit_5d}_*.dat" 2>/dev/null)

if [ -z "$runsPaths" ]; then
  echo "Run $runNumber not found under $inputFolder"; echo ""; exit 1
fi

echo "Found the following paths:"; echo "$runsPaths"

echo "Removing duplicates by filename..."
uniqueRunsPaths=""
for runPath in $runsPaths; do
  runFilename=$(basename "$runPath")
  if [[ $uniqueRunsPaths != *"$runFilename"* ]]; then
    uniqueRunsPaths="$uniqueRunsPaths $runPath"
  fi
done

uniqueRunsPaths=$(echo "$uniqueRunsPaths" | xargs)

echo "Unique path(s):"
echo "$uniqueRunsPaths"
