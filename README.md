# pDUNE Oca Data Analyzer

Software to analyze the data coming from the beam NP02 beam plug tracker.
The DAQ is [here](https://github.com/emanuele-villa/oca-pDUNE-DAQ/tree/master).
This code has been forked from the one originally written by INFN Perugia.

The data can be found at `/eos/user/e/evilla/dune/np02-beam-monitor`, request access to emanuele.villa@cern.ch.
Ask Emanuele for the url if you want to download from web interface.

## Install

After cloning, the submodules need to be initialized and updated, there is a script for that.
You can also check if compilation succeeds, even though it's run in all other scripts.

```bash
git clone https://github.com/emanuele-villa/oca-pDUNE-dataAnalyzer.git
./scripts/manage-submodules.sh --up
./scripts/compile.sh
```

To use the code in your local machine, you need to have:

- ROOT installed in the standard location, meaning `/usr/local/`,
- json header: `json.hpp`;
- cmake (minimum 3.17);
- gcc version at least 11, or a recent clang;

If needed, install or update these packages.
In lxplus (recommended), all dependencies are fine when sourcing an image through `source scripts/lxplus-image.sh`. 
This is done automatically in the `init.sh` script, called by any other script, so it's not needed to run it manually.

## Usage

Scripts handle all the steps of compilation, data conversion and analysis.
The scripts are located in the `scripts` folder.
Input and output folders are set in a json file passed through the command line.
Create your own json file by copying the `settings_template.json` file and modifying it. 
Input path is already the correct one, choose your local output path.


The script `analyzeRuns.sh` is the main script to analyze the data.
It can also compile the code, see --help option to show the available options.
Example of usage:
    
```bash
./analyzeRuns.sh -j json/mysettings.json -r SCD_RUN00021_CAL_20240826_160235.dat 
```

Or you can use the run number(s), where f and l mean first and last:
        
```bash
./analyzeRuns.sh -f 10 -l 12 -j json/mysettings.json
```

Use only -f to analyze a single run.
Important parameter to consider something a signal is `nSigma`, which is the number of standard deviations above the pedestal to consider a signal. 
It can be set in the json file, or passed as an argument to the script, e.g. `-s 5` for 5 sigma.
The app produces a pdf report with the analysis results, and a root file with the data.

There is then an app to convert from raw data to root files, `bmRawToRootConverter.sh`, for further analysis:

```bash
./bmRawToRootConverter.sh -j json/mysettings.json -r 21
```

## Structure

The actual steps that are performed by the scripts are:

- compile the code, stopping execution if the compilation fails;
- convert the raw data into root files using the `PAPERO_convert` executable;
- create the calibration file, that extracts the pedestal by channel, using the `calibration` executable;
- read calib and root files and do some analysis, using the `dataAnalyzer   ` executable;

## Other tools

There is also a `rav_viewer` executable that can be used to visualize the raw data in a GUI.
Other tools for other applications have been dropped, see the fork or other branches for those.
