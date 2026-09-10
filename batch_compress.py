import sys
import os
import re
import argparse
import subprocess

rawfile_folder = '../CernBox/CRSPACE/'
root_folder = '../CernBox/RootFiles/'

def get_eos_available_space(quota_node="/eos/user/g/gisilves/"):
    # Execute the eos quota command
    output = subprocess.check_output(["eos", "quota", quota_node], text=True)

    # Extract the fill percentage value from the output table
    match = re.search(r'(\d+(?:\.\d+)?)\s*%', output)
    if not match:
        raise ValueError("Could not parse filled percentage from output.")
    
    filled_percentage = float(match.group(1))

    # Retrieve available space in bytes
    available_percentage = 100.0 - filled_percentage
    available_space = available_percentage * 10**10
    
    return available_space

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

    # Check file size
    filesize = os.path.getsize(rawfile_folder + datafile)

    # Check available space: if less than input file size, exit
    available_space = get_eos_available_space()
    if available_space < filesize:
        if not silent:
            print('\tNot enough space available on eos')
        return

    # Run data conversion command
    if not silent:
        command = './HEF_convert ' + rawfile_folder + datafile + ' ' + root_folder + 'run' + str(datarun).zfill(5) + '.root' + ' --compression 5 --nevents ' + str(nevents)
    else:
        command = './HEF_convert ' + rawfile_folder + datafile + ' ' + root_folder + 'run' + str(datarun).zfill(5) + '.root' + ' --compression 5 --nevents ' + str(nevents) + ' --silent'
    print(command)
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
            process_data(datarun, args.nevents, silent=False)

if __name__ == '__main__':
    main()