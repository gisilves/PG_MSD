import sys
import os
import re
import argparse
import subprocess

rawfile_folder = '/mnt/nas/202211_SCD_data/data/'
root_folder = '/mnt/f/202211_SCD_data/RootData/'

def process_data(datarun, nevents, silent=False):
    # Find data .dat file in rawfiles folder
    # Filename format: SCD_RUN#####_MIX_YYYYMMDD_HHMMSS.dat
    # where ##### is the data run number 0 padded to 5 digits
    
    datafile = None
    search_string = 'SCD_RUN' + str(datarun).zfill(5) + '_BEAM_'

    for filename in os.listdir(rawfile_folder):
        if filename.startswith(search_string):
            datafile = filename
            break
    if not datafile:
        if not silent:
            print('\tData file not found')
        return

    if not silent:
        print('\tFound data file {}'.format(datafile))

    # Run data conversion command
    command = './PAPERO_convert ' + rawfile_folder + datafile + ' ' + root_folder + 'run' + str(datarun).zfill(5) + '.root'
    print(command)
    subprocess.run(command, shell=True)

    return root_folder + 'run' + str(datarun).zfill(5) + '.root'

def main():
    parser = argparse.ArgumentParser(description='Process PAPERO data files to obtain rootfiles')
    parser.add_argument('--datarun', type=int, help='Data run number')
    parser.add_argument('--nevents', type=int, help='Number of events to process')
    parser.add_argument('--runlist', type=str, help='Runlist file')
    args = parser.parse_args()

    if not args.nevents:
        args.nevents = -1
    
    if not args.runlist:
        if not args.datarun:
            print('Please provide data run number (or runlist file)')
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
            process_data(datarun, args.nevents, silent=False)
    else:
        process_data(args.datarun, args.nevents, silent=False)

if __name__ == '__main__':
    main()
