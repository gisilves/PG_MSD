import sys
import os
import argparse
import subprocess

rawfile_folder = '/mnt/e/CRSPACE/'
calib_folder = '/mnt/f/CRSPACE/calfiles/'

def process_cal(calrun, silent=False):
    # Find calibration .dat file in rawfiles folder
    # Filename format: SCD_RUN#####_CAL_YYYYMMDD_HHMMSS.dat
    # where ##### is the calibration run number 0 padded to 5 digits
    
    calfile = None
    search_string = 'SCD_RUN' + str(calrun).zfill(5) + '_CAL_'

    for filename in os.listdir(rawfile_folder):
        if filename.startswith(search_string):
            calfile = filename
            break
    if not calfile:
        if not silent:
            print('\tCalibration file not found')
        return

    if not silent:
        print('\tFound calibration file {}'.format(calfile))

    outfile_name = calib_folder + 'calib_run' + str(calrun).zfill(5) + '.cal'
    if os.path.exists(outfile_name):
        if not silent:
            print('\tCalibration file already exists')
        return outfile_name

    # Run calibration command
    if not silent:
        command = './calibration --raw --fast --nevents 5000 ' + rawfile_folder + calfile + ' --output ' + calib_folder + 'calib_run' + str(calrun).zfill(5)
    else:
        command = './calibration --raw --fast --silent --nevents 5000 ' + rawfile_folder + calfile + ' --output ' + calib_folder + 'calib_run' + str(calrun).zfill(5) + ' 2> /dev/null'
    subprocess.run(command, shell=True)
 
    return calib_folder + 'calib_run' + str(calrun).zfill(5) + '.cal'

def main():
    parser = argparse.ArgumentParser(description='Process HEF data files to obtain rootfiles')
    parser.add_argument('--calrun', type=int, help='Calibration run number')
    parser.add_argument('--nevents', type=int, help='Number of events to process')
    parser.add_argument('--runlist', type=str, help='Runlist file')
    args = parser.parse_args()

    if not args.nevents:
        args.nevents = -1
    
    if not args.runlist:
        if not args.calrun:
            print('Please provide calibration run number (or runlist file)')
            parser.print_help()
            sys.exit(1)
        
    if args.runlist:
        runlist = args.runlist
        print('\nProcessing runlist file {}'.format(runlist))
        
        runs = []
        with open(runlist, 'r') as f:
            for line in f:
                line = line.strip()
                if "#" in line:
                    continue
                if line:
                    calrun, datarun = line.split()
                    runs.append((int(calrun), int(datarun)))
                    
        for i, (calrun, datarun) in enumerate(runs, 1):
            process_cal(calrun, silent=False)

if __name__ == '__main__':
    main()