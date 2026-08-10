import sys
import os
import argparse
import subprocess

rawfile_folder = './rawfiles/'
calib_folder = './calfiles/'
root_folder = './rootfiles/'
cluster_folder = './clusfiles/'

def process_cal(calrun):
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
        print('\tCalibration file not found')
        return

    print('\tFound calibration file {}'.format(calfile))

    outfile_name = calib_folder + 'calib_run' + str(calrun).zfill(5) + '.cal'
    if os.path.exists(outfile_name):
        print('\tCalibration file already exists')
        return outfile_name

    # Run calibration command
    command = './calibration --raw --fast --nevents 5000 ' + rawfile_folder + calfile + ' --output ' + calib_folder + 'calib_run' + str(calrun).zfill(5)
    subprocess.run(command, shell=True)
 
    return calib_folder + 'calib_run' + str(calrun).zfill(5) + '.cal'

def process_data(datarun):
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
        print('\tData file not found')
        return

    print('\tFound data file {}'.format(datafile))

    # Run data conversion command
    command = './HEF_convert ' + rawfile_folder + datafile + ' ' + root_folder + 'run' + str(datarun).zfill(5) + '.root'
    subprocess.run(command, shell=True)

    return root_folder + 'run' + str(datarun).zfill(5) + '.root'

def process_cluster(calfile, datarootfile, datarun):
    # Run clustering command
    command = './raw_clusterize --version 2026 --input_files {} --calibration_file {} --output_file {}'.format(datarootfile, calfile, cluster_folder + 'clus_run' + str(datarun).zfill(5))
    subprocess.run(command, shell=True)

def main():
    parser = argparse.ArgumentParser(description='Process SCD calibration and data files to obtain clusters')
    parser.add_argument('--calrun', type=int, help='Calibration run number')
    parser.add_argument('--datarun', type=int, help='Data run number')
    args = parser.parse_args()

    # If no arguments are provided, print help message and exit
    if not args.calrun or not args.datarun:
        print('Please provide both calibration and data run numbers')
        parser.print_help()
        sys.exit(1)

    print('\nProcessing calibration files for run {}'.format(args.calrun))
    calfile = process_cal(args.calrun)

    print('\nProcessing data files for run {}'.format(args.datarun))
    datafile = process_data(args.datarun)

    print('\nProcessing clustering for run {}'.format(args.datarun))
    process_cluster(calfile, datafile, args.datarun)

if __name__ == '__main__':
    main()