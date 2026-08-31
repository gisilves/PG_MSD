import sys
import os
import argparse
import subprocess

rawfile_folder = '/mnt/e/CRSPACE/'
root_folder = '/mnt/f/CRSPACE/rootfiles/'

def process_data(datarun, nevents, silent=False):
    # Find data .dat file in rawfiles folder
    # Filename format: SCD_RUN#####_MIX_YYYYMMDD_HHMMSS.dat
    # where ##### is the data run number 0 padded to 5 digits
    
    datafile = None
    search_string = 'SCD_RUN' + str(datarun).zfill(5) + '_MIX_'

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
    if not silent:
        command = './HEF_convert ' + rawfile_folder + datafile + ' ' + root_folder + 'run' + str(datarun).zfill(5) + '.root' + ' --nevents ' + str(nevents)
    else:
        command = './HEF_convert ' + rawfile_folder + datafile + ' ' + root_folder + 'run' + str(datarun).zfill(5) + '.root' + ' --nevents ' + str(nevents) + ' --silent'
    subprocess.run(command, shell=True)

    return root_folder + 'run' + str(datarun).zfill(5) + '.root'

def main():
    parser = argparse.ArgumentParser(description='Process HEF data files to obtain rootfiles')
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
            process_data(datarun, args.nevents, silent=True)

if __name__ == '__main__':
    main()