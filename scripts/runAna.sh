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
    echo "  --clean-files       clean the output files before running the analysis. Default is to not clean the output files"
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
      --clean-files)      cleanFiles=true; shift ;;
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
. $compile_command
if [ $? -ne 0 ]; then
    echo "Error: Compilation failed. Stopping execution."
    exit 1
fi

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

    # clean output files if requested
    if [ "$cleanFiles" = true ]
    then
        echo "Cleaning output files in $outputDirectory for run $runit"
        rm -f ${outputDirectory}/SCD_RUN$(printf "%05d" $runit)*.root
        rm -f ${outputDirectory}/SCD_RUN$(printf "%05d" $runit)*.cal
    fi

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

    if [ -n "$calRunPath" ]; then
      calBase=$(basename "$calRunPath")
      calBase=${calBase%.*}
      # Ensure previous CAL run converted and calibrated
      if [ ! -f "${outputDirectory}/${calBase}_converted.root" ]; then
      echo "Converting previous CAL run for calibration: $calRunPath"
      prev_convert_data="./flat_convert ${calRunPath} ${outputDirectory}/${calBase}_converted.root --dune"
      echo "Executing command: $prev_convert_data"
      $prev_convert_data
      if [ $? -ne 0 ]; then
        echo "Error: Failed to convert CAL run $calRunPath for calibration. Stopping execution."
        exit 1
      fi
      fi
      if [ ! -f "${outputDirectory}/${calBase}.cal" ]; then
      echo "Extracting calibration from previous CAL run: ${calBase}.root"
      prev_extract_cal="./calibration ${outputDirectory}/${calBase}_converted.root --output ${outputDirectory}/${calBase} --dune --fast"
      echo "Executing command: $prev_extract_cal"
      $prev_extract_cal
      if [ $? -ne 0 ]; then
        echo "Error: Failed to extract calibration from CAL run $calBase. Stopping execution."
        exit 1
      fi
      fi
      calFileToUse="${outputDirectory}/${calBase}.cal"
    else
      echo "Error: No previous CAL run found before run ${runit}. Stopping execution."
      exit 1
    fi

    # Verify calibration file exists
    if [ -z "$calFileToUse" ] || [ ! -f "$calFileToUse" ]; then
      echo "Error: Calibration file not found for run ${runit}. Expected: $calFileToUse"
      echo "Stopping execution."
      exit 1
    fi

    echo "Using calibration file: $calFileToUse"

    # Check if conversion is needed (file doesn't exist or calibration changed)
    needsConversion=true
    if [ -f "${outputDirectory}/${fileName}_converted.root" ]; then
        # Check if the converted file has the required detector trees
        if root -l -q "${outputDirectory}/${fileName}_converted.root" -e "TFile f(\"${outputDirectory}/${fileName}_converted.root\"); if (f.Get(\"raw_detector0\")) { cout << \"TREE_EXISTS\" << endl; } else { cout << \"TREE_MISSING\" << endl; }" 2>/dev/null | grep -q "TREE_EXISTS"; then
            echo "Converted file exists and has required trees. Checking if calibration matches..."
            # For now, we'll force re-conversion to ensure calibration is correct
            # TODO: Add logic to check if calibration file used matches expected
            echo "Forcing re-conversion to ensure correct calibration is used."
            rm -f "${outputDirectory}/${fileName}_converted.root"
        else
            echo "Converted file exists but is missing required trees. Will re-convert."
            rm -f "${outputDirectory}/${fileName}_converted.root"
        fi
    fi

    if [ "$needsConversion" = true ] || [ ! -f "${outputDirectory}/${fileName}_converted.root" ]; then
        convert_data="./flat_convert ${filePath} ${outputDirectory}/${fileName}_converted.root --cal-file $calFileToUse --dune"
        echo "Executing command: "$convert_data
        $convert_data
        if [ $? -ne 0 ]; then
            echo "Error: Data conversion failed for run ${runit}. Skipping analysis for this run."
            continue
        fi
    else
        echo "File ${outputDirectory}/${fileName}_converted.root already exists with correct calibration. Skipping conversion."
    fi
  fi

  # Create a formatted file from the converted ROOT and feed it to clustering
  formattedFile="${outputDirectory}/${fileName}_formatted.root"

  if [ -f "${formattedFile}" ]; then
    echo "Formatted file already exists: ${formattedFile}. Skipping formatting step."
  else
    formatting_command="./formatting --cal-file ${calFileToUse} --dune ${outputDirectory}/${fileName}_converted.root ${formattedFile}"
    echo "Executing command: ${formatting_command}"
    $formatting_command
    if [ $? -ne 0 ]; then
      echo "Error: Formatting failed for run ${runit}. Skipping clustering and report generation for this run."
      continue
    fi
  fi

  clustering_command="./clustering -i ${formattedFile} -o ${outputDirectory}/${fileName}_clusters.root -s ${nsigma}"

  # if [ "$verbose" = true ]
  # then
  #     echo "Verbose mode is on."
  #     clustering_command+=" -v"
  # fi

  echo "Executing command: ${clustering_command}"
  $clustering_command
  if [ $? -ne 0 ]; then
    echo "Error: Clustering failed for run ${runit}. Skipping report generation for this run."
    continue
  fi


  runReport_command="./runReport -i ${outputDirectory}/${fileName}_clusters.root -o ${outputDirectory} -s ${nsigma} -v"
  
  if [ "$verbose" = true ]
  then
      echo "Verbose mode is on."
      runReport_command+=" -v"
  fi

  if [ "$debug" = true ]
  then
      echo "Debug mode is on."
      runReport_command+=" -d"
  fi

  echo "Executing command: "$runReport_command
  $runReport_command
  if [ $? -ne 0 ]; then
      echo "Error: Report generation failed for run ${runit}."
      # Don't continue here as this is the last step
  fi

  fileName="" # reset fileName for the next iteration
 
done

echo "All runs have been analyzed. Exiting."