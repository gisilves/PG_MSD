# pDUNE Oca Data Analyzer

Software to analyze the data coming from the beam NP02 beam plug tracker.
The DAQ is [here](https://github.com/emanuele-villa/oca-pDUNE-DAQ/tree/master).
This code has been forked from the one originally written by INFN Perugia.

## Dependencies

To use the code in your local machine, you need to have:

- ROOT installed in the standard location, meaning `/usr/local/`,
- json header: `json.hpp`;
- cmake (minimum 3.17);
- gcc version at least 11, or a recent clang;

If needed, install or update these packages.

## Usage

Scripts handle all the steps of compilation, data conversion and analysis.
The scripts are located in the `scripts` folder.
Input and output folders are set in a json file, `config.json`, that can be found in 


The script `analyzeRuns.sh` is the main script to analyze the data.
It comes with a --help option to show the available options.
Example of usage:
    
```bash
./analyzeRuns.sh -s json/mysettings.json -r SCD_RUN00021_CAL_20240826_160235.dat 
```

Or you can use the run numbers (**not available yet, need to change the daq**):
        
```bash
./analyzeRuns.sh -f 10 -l 12
```

This will analyze the run `SCD_RUN00021_CAL_20240826_160235.dat` and produce the output in the `output` folder that has been set in the json file.

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
