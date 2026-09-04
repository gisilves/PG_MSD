import sys
import argparse
import subprocess

root_folder = '/mnt/f/CRSPACE/rootfiles/'
cal_folder = '/mnt/f/CRSPACE/calfiles/'
clus_folder = '/mnt/f/CRSPACE/clusfiles/'

def process_cluster(calrun, datarun, silent=False):
    # Run clustering command
    input_file = root_folder + 'run' + str(datarun).zfill(5) + '.root'
    calfile = cal_folder + 'calib_run' + str(calrun).zfill(5) + '.cal'
    output_file = clus_folder + 'clus_run' + str(datarun).zfill(5)
    if not silent:
        command = './raw_clusterize --input_files {} --calibration_file {} --output_file {}'.format(input_file, calfile, output_file)
    else:
        command = './raw_clusterize --input_files {} --calibration_file {} --output_file {} --silent'.format(input_file, calfile, output_file)
    subprocess.run(command, shell=True)

def main():
    parser = argparse.ArgumentParser(description='Process HEF data files to obtain rootfiles')
    parser.add_argument('--calrun', type=int, help='Calibration run number')
    parser.add_argument('--datarun', type=int, help='Data run number')
    parser.add_argument('--runlist', type=str, help='Runlist file')
    args = parser.parse_args()
    
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
            process_cluster(calrun, datarun, i)
    else:
        process_cluster(args.calrun, args.datarun, 1)

if __name__ == '__main__':
    main()