#!/bin/bash

export SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source $SCRIPTS_DIR/init.sh

# Set the default input and output directories, to be overwritten by the settings
inputDirectory="" 
outputDirectory=$HOME_DIR"/output/"

# Initialize variables
settingsFile=""
fileName=""
noCompile=false
cleanCompile=false
nsigma=5 # Default value for nSigma

# Function to print help message
print_help() {
    echo "*****************************************************************************"
    echo "Usage: $0 -f <first_run> [-l <last_run>] -j <settings_file> [-h]"
    echo "  -p | --home-path      path to the code home directory (/your/path/tof-reco, no / at the end). There is no default."
    echo "  -r | --run-name       full run name, with ot without .dat"
    echo "  -s | --n-sigma       number of sigma to use for the analysis. Will overwrite whatever is in json settings."
    echo "  -f | --first-run      first run number"
    echo "  -l | --last-run       last run number" 
    echo "  -j | --json-settings  json settings file to run this app"
    echo "  --no-compile          do not recompile the code. Default is to recompile the code"
    echo "  --clean-compile       clean and recompile the code. Default is to recompile the code without cleaning"
    echo "  --source-lxplus       source the lxplus environment. Default is to not source the lxplus environment"
    echo "  -h | --help           print this help message"
    echo "*****************************************************************************"
    exit 0
}

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
      -p|--home-path)      HOME_DIR="$2"; shift 2 ;;
      -s|--n-sigma)        nsigma="$2"; shift 2 ;;
      -r|--run-name)       fileName="$2"; shift 2 ;;
      -f|--first-run)      firstRun="$2"; shift 2 ;;
      -l|--last-run)       lastRun="$2"; shift 2 ;;
      -j|--json-settings)  settingsFile="$2"; shift 2 ;;
      --no-compile)        noCompile=true; shift ;;
      --clean-compile)     cleanCompile=true; shift ;;
      --source-lxplus)     sourceLxplus=true; shift ;;
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
echo "Currently in $(pwd), moving to ${HOME_DIR} to run the script." # might not be necessary if using always absolute paths
cd $HOME_DIR

# read from settings file the input and output paths
inputDirectory=$(awk -F'"' '/inputDirectory/{print $4}' "$settingsFile")
outputDirectory=$(awk -F'"' '/outputDirectory/{print $4}' "$settingsFile")
verbose=$(awk -F'"' '/verboseMode/{print $4}' "$settingsFile")
debug=$(awk -F'"' '/debugMode/{print $4}' "$settingsFile")

# Only read nsigma from settings if not provided via command line
if [ -z "$nsigma" ]; then
    nsigma=$(awk -F'"' '/nSigma/{print $4}' "$settingsFile") # TODO add fallback if not defined
fi

# Check if the user has selected a run name or number(s)
if [ -z "$fileName" ] 
then
    if [ -z "$firstRun" ]
    then
    echo "Please select a run number (or an interval) from command line to convert the data into ROOT format. 
    You can select only the first run, or both the first and the last run.
    Usage: 
     $0 -f <first_run> [-l <last_run>] -j <json_settings_file> [-h]"
    exit 0
    fi
    if [ -z "$lastRun" ]
    then
    echo "I have received only the number of the first run. Reading only run $firstRun"
    lastRun=$firstRun
    fi
else
    echo "I have received the run name: $fileName"
    # if it has the extension, remove it
    fileName=$(echo $fileName | awk -F'.' '{print $1}')
    # firstRun=$(echo $fileName | awk -F'_' '{print $2}')
fi

################################################
# Compile the code if requested
compile_command="$SCRIPTS_DIR/compile.sh -p $HOME_DIR --no-compile $noCompile --clean-compile $cleanCompile"
echo "Compiling the code with the following command:"
echo $compile_command
. $compile_command || exit

################################################
# Execute the code
cd $HOME_DIR/build # executable is here, for now

if [ -n "$fileName" ]
then
  # just to get in the loop below
  firstRun=0
  lastRun=0
fi

# Iterate over all the selected runs
for ((runit = $firstRun; runit <= $lastRun; runit++ ))
do
  if [ -z $fileName ]
  then
    echo "Looking for files by run number"
    echo "runit: $runit"
    runit_5d=$(printf "%05d" "$runit")
    # Prefer shared finder utility
    filePath=$($SCRIPTS_DIR/findRun.sh -r "$runit" -i "$inputDirectory" | tail -n 1)
    
    # Stop execution if the selected run is not present in the input directory
    if [ -z "$filePath" ]
    then
        echo "Run "$runit " not found. Stopping execution."
        continue
    fi

    echo "For this run number, found the following file(s):"
    echo "$filePath"

    echo "Dealing with file: $filePath"
    echo "outputDirectory: $outputDirectory"

    # in case it doesn't exist, creating output directory
    mkdir -p "$outputDirectory"

    echo ""
    echo "Currently we are in"
    pwd
    echo ""

    fileName=$(basename "$filePath")
    fileName=${fileName%.*}
    echo "File name is: $fileName"
  else
    echo "Received a single file path ${fileName}"
    filePath="${inputDirectory}/${fileName}.dat"
  fi
  if [ -f "${outputDirectory}/${fileName}_converted.root" ]
  then
      echo "File ${outputDirectory}/${fileName}_converted.root already exists. Skipping conversion."
  else
      convert_data="./PAPERO_convert ${filePath} ${outputDirectory}/${fileName}_converted.root --dune"
      echo "Executing command: "$convert_data
      $convert_data
  fi

  # Determine calibration strategy: use previous CAL run's .cal (fallback: next CAL; if current is CAL, use its own)
  currentBaseName=$(basename "$filePath")
  currentIsCAL=false
  if echo "$currentBaseName" | grep -qi "CAL"; then
    currentIsCAL=true
  fi

  # Search backwards for the most recent previous run whose filename contains "CAL"
  calRunPath=""
  prevRun=$((runit-1))
  while [ $prevRun -ge 0 ]; do
    prevPaths=$($SCRIPTS_DIR/findRun.sh -r "$prevRun" -i "$inputDirectory" 2>/dev/null | tail -n 1)
    if [ -n "$prevPaths" ]; then
      for p in $prevPaths; do
        if basename "$p" | grep -qi "CAL"; then
          calRunPath="$p"
          break
        fi
      done
    fi
    if [ -n "$calRunPath" ]; then
      break
    fi
    prevRun=$((prevRun-1))
  done

  calFileToUse=""
  if [ -n "$calRunPath" ]; then
    calBase=$(basename "$calRunPath")
    calBase=${calBase%.*}
    # Ensure previous CAL run converted and calibrated
    if [ ! -f "${outputDirectory}/${calBase}_converted.root" ]; then
      echo "Converting previous CAL run for calibration: $calRunPath"
      prev_convert_data="./PAPERO_convert ${calRunPath} ${outputDirectory}/${calBase}_converted.root --dune"
      echo "Executing command: $prev_convert_data"
      $prev_convert_data
    fi
    if [ ! -f "${outputDirectory}/${calBase}.cal" ]; then
      echo "Extracting calibration from previous CAL run: ${calBase}.root"
      prev_extract_cal="./calibration ${outputDirectory}/${calBase}_converted.root --output ${outputDirectory}/${calBase} --dune --fast"
      echo "Executing command: $prev_extract_cal"
      $prev_extract_cal
    fi
    calFileToUse="${outputDirectory}/${calBase}.cal"
  else
    echo "Warning: No previous CAL run found before run ${runit}."
    # Fallback 1: search for the nearest later CAL run in the input directory
    echo "Searching for the nearest later CAL run as fallback..."
    calCandidates=$(find "$inputDirectory" -type f -name "SCD_RUN*.dat" 2>/dev/null | grep -i "CAL")
    nextCalNum=""
    nextCalPath=""
    while IFS= read -r p; do
      [ -z "$p" ] && continue
      rn=$(basename "$p" | sed -n 's/.*SCD_RUN\([0-9]\{5\}\)_.*/\1/p')
      if [ -n "$rn" ]; then
        # force decimal to avoid octal
        val=$((10#$rn))
        if [ $val -gt $runit ]; then
          if [ -z "$nextCalNum" ] || [ $val -lt $nextCalNum ]; then
            nextCalNum=$val
            nextCalPath="$p"
          fi
        fi
      fi
    done <<< "$calCandidates"

    if [ -n "$nextCalPath" ]; then
      echo "Using next CAL run as fallback: $nextCalPath"
      calBase=$(basename "$nextCalPath")
      calBase=${calBase%.*}
      if [ ! -f "${outputDirectory}/${calBase}_converted.root" ]; then
        echo "Converting next CAL run for calibration: $nextCalPath"
        next_convert_data="./PAPERO_convert ${nextCalPath} ${outputDirectory}/${calBase}_converted.root --dune"
        echo "Executing command: $next_convert_data"
        $next_convert_data
      fi
      if [ ! -f "${outputDirectory}/${calBase}.cal" ]; then
        echo "Extracting calibration from next CAL run: ${calBase}.root"
        next_extract_cal="./calibration ${outputDirectory}/${calBase}_converted.root --output ${outputDirectory}/${calBase} --dune --fast"
        echo "Executing command: $next_extract_cal"
        $next_extract_cal
      fi
      calFileToUse="${outputDirectory}/${calBase}.cal"
    fi

    # Fallback 2: if current run is CAL, ensure its calibration exists and use it
    if [ "$currentIsCAL" = true ]; then
      if [ ! -f "${outputDirectory}/${fileName}.cal" ]; then
        extract_calibration="./calibration ${outputDirectory}/${fileName}_converted.root --output ${outputDirectory}/${fileName} --dune --fast"
        echo "Executing command: $extract_calibration"
        $extract_calibration
      fi
      calFileToUse="${outputDirectory}/${fileName}.cal"
    else
      echo "Warning: Current run is not CAL and no previous CAL found; analysis will attempt with current .cal if present."
      calFileToUse="${outputDirectory}/${fileName}.cal"
    fi
  fi

  # Only extract calibration for the current run if it is a CAL run
  if [ "$currentIsCAL" = true ]; then
    if [ -f "${outputDirectory}/${fileName}.cal" ]; then
      echo "Calibration for current CAL run already exists: ${outputDirectory}/${fileName}.cal"
    else
      extract_calibration="./calibration ${outputDirectory}/${fileName}_converted.root --output ${outputDirectory}/${fileName} --dune --fast"
      echo "Executing command: $extract_calibration"
      $extract_calibration
    fi
  else
    echo "Current run is not a CAL run; skipping calibration extraction for this run."
  fi

  # Verify calibration file exists
  if [ -z "$calFileToUse" ] || [ ! -f "$calFileToUse" ]; then
      echo "Error: Calibration file not found for run ${runit}. Expected: $calFileToUse"
      echo "Skipping analysis for this run."
      continue
  fi

  analyze_data="./dataAnalyzer -r ${outputDirectory}/${fileName}_converted.root -c ${calFileToUse} -o ${outputDirectory} -s ${nsigma} -n ${runit} -j ${settingsFile}"
  
  if [ "$verbose" = true ]
  then
      echo "Verbose mode is on."
      analyze_data+=" -v"
  fi

  if [ "$debug" = true ]
  then
      echo "Debug mode is on."
      analyze_data+=" -d"
  fi

  echo "Executing command: "$analyze_data
  $analyze_data

  fileName="" # reset fileName for the next iteration
 
done

echo "All runs have been analyzed. Exiting."