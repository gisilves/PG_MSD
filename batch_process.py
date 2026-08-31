import sys
import os
import argparse
import subprocess
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import defaultdict
import time

rawfile_folder = '/mnt/e/CRSPACE/'
calib_folder = '/mnt/f/CRSPACE/calfiles/'
root_folder = '/mnt/f/CRSPACE/rootfiles/'
cluster_folder = '/mnt/f/CRSPACE/clusfiles/'

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

def process_cluster(calfile, datarootfile, datarun, silent=False):
    # Run clustering command
    if not silent:
        command = './raw_clusterize --input_files {} --calibration_file {} --output_file {}'.format(datarootfile, calfile, cluster_folder + 'clus_run' + str(datarun).zfill(5))
    else:
        command = './raw_clusterize --input_files {} --calibration_file {} --output_file {} --silent'.format(datarootfile, calfile, cluster_folder + 'clus_run' + str(datarun).zfill(5))
    subprocess.run(command, shell=True)

def print_table(status_dict, thread_num, calrun, datarun, threads = 1):
    """Print progress table in thread-safe manner"""
    print("\n")
    print('{:<6} {:<10} {:<10} {:<12} {:<12} {:<12}'.format(
        thread_num, calrun, datarun,
        status_dict[(calrun, datarun)]['cal'],
        status_dict[(calrun, datarun)]['convert'],
        status_dict[(calrun, datarun)]['cluster']
    ))

def process_run(calrun, datarun, nevents, thread_num, status_dict, lock, silent=False):
    """Process a single run: calibration -> data conversion -> clustering"""
    try:
        key = (calrun, datarun)
        status_dict[key] = {'cal': 'RUNNING', 'convert': 'PENDING', 'cluster': 'PENDING'}
        
        with lock:
            print_table(status_dict, thread_num, calrun, datarun)
        
        calfile = process_cal(calrun, silent)
        
        if not calfile:
            status_dict[key]['cal'] = 'FAILED'
            with lock:
                print_table(status_dict, thread_num, calrun, datarun)
            return False
        
        status_dict[key]['cal'] = 'DONE'
        status_dict[key]['convert'] = 'RUNNING'
        with lock:
            print_table(status_dict, thread_num, calrun, datarun)
        
        datafile = process_data(datarun, nevents, silent)
        
        if not datafile:
            status_dict[key]['convert'] = 'FAILED'
            with lock:
                print_table(status_dict, thread_num, calrun, datarun)
            return False
        
        status_dict[key]['convert'] = 'DONE'
        status_dict[key]['cluster'] = 'RUNNING'
        with lock:
            print_table(status_dict, thread_num, calrun, datarun)
        
        process_cluster(calfile, datafile, datarun, silent)
        
        status_dict[key]['cluster'] = 'DONE'
        with lock:
            print_table(status_dict, thread_num, calrun, datarun)
        
        return True
    except Exception as e:
        status_dict[key] = {'cal': 'ERROR', 'convert': 'ERROR', 'cluster': 'ERROR'}
        with lock:
            print_table(status_dict, thread_num, calrun, datarun)
        return False

def main():
    parser = argparse.ArgumentParser(description='Process SCD calibration and data files to obtain clusters')
    parser.add_argument('--calrun', type=int, help='Calibration run number')
    parser.add_argument('--datarun', type=int, help='Data run number')
    parser.add_argument('--nevents', type=int, help='Number of events to process')
    parser.add_argument('--runlist', type=str, help='Runlist file')
    parser.add_argument('--threads', type=int, help='Number of threads to use')
    args = parser.parse_args()

    if not args.nevents:
        args.nevents = -1
    
    if not args.threads and args.runlist:
        print('No threads specified, using 1')
        args.threads = 0
        
    if args.threads and not args.runlist:
        print('No runlist specified, using 1 thread')
        args.threads = 0

    if not args.runlist:
        if not args.calrun or not args.datarun:
            print('Please provide calibration and data run numbers (or runlist file)')
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
        
        if args.threads and args.threads > 1:
            silent = True
            print('Processing {} runs with {} threads\n'.format(len(runs), args.threads))
            print('{:<6} {:<10} {:<10} {:<12} {:<12} {:<12}'.format(
                'THREAD', 'CALRUN', 'DATARUN', 'CAL', 'CONVERT', 'CLUSTERIZE'))
            print('-' * 70)
            
            status_dict = {}
            lock = threading.Lock()
            
            with ThreadPoolExecutor(max_workers=args.threads - 1) as executor:
                futures = {
                    executor.submit(process_run, calrun, datarun, args.nevents, i, status_dict, lock, silent): (calrun, datarun, i)
                    for i, (calrun, datarun) in enumerate(runs, 1)
                }
                
                for future in as_completed(futures):
                    calrun, datarun, thread_num = futures[future]
                    try:
                        result = future.result()
                    except Exception as e:
                        pass
            
            print('\n')
        else:
            print('Processing {} runs sequentially\n'.format(len(runs)))
            print('{:<6} {:<10} {:<10} {:<12} {:<12} {:<12}'.format(
                'THREAD', 'CALRUN', 'DATARUN', 'CAL', 'CONVERT', 'CLUSTERIZE'))
            print('-' * 70)
            
            status_dict = {}
            lock = threading.Lock()
            
            for i, (calrun, datarun) in enumerate(runs, 1):
                process_run(calrun, datarun, args.nevents, i, status_dict, lock)
            
            print('\n')
    else:
        print('{:<6} {:<10} {:<10} {:<12} {:<12} {:<12}'.format(
            'THREAD', 'CALRUN', 'DATARUN', 'CAL', 'CONVERT', 'CLUSTERIZE'))
        print('-' * 70)
        
        status_dict = {}
        lock = threading.Lock()
        
        process_run(args.calrun, args.datarun, args.nevents, 1, status_dict, lock)
        print('\n')

if __name__ == '__main__':
    main()