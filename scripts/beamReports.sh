#!/bin/bash

# export SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# source $SCRIPTS_DIR/init.sh

# Function to print help message
print_help() {
    echo "*****************************************************************************"
    echo "Usage: $0 -f <first_run> [-l <last_run>]  [-h]"
    echo "  -f | --first-run      first run number"
    echo "  -l | --last-run       last run number" 
    echo "  -h | --help           print this help message"
    echo "*****************************************************************************"
    exit 0
}

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
      -f|--first-run)      firstRun="$2"; shift 2 ;;
      -l|--last-run)       lastRun="$2"; shift 2 ;;
      -h|--help)           print_help ;;
      *)                   shift ;;
    esac
done

####################
# Check if the user has selected a run name or number(s)
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

################################################
# Iterate over all the selected runs
for ((runit = $firstRun; runit <= $lastRun; runit++ ))
do
  runit_5d=$(printf "%05d" "$runit")
  
  outfolder="/eos/user/e/evilla/dune/np02-beam-monitor/"
  reports_path="$outfolder/reports/"
  beam_reports_path="$outfolder/beam_reports/"
  cp $reports_path/*${runit_5d}*BEAM*5sigma_report.pdf $beam_reports_path
 
done

echo "Moved all requested runs to $beam_reports_path."