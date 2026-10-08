import sys
import os
import argparse
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from tqdm import tqdm


rawfile_folder = '/mnt/nas/202211_SCD_data/data/'
root_folder = '/mnt/f/202211_SCD_data/RootData/'

def process_data(datarun, nevents):
    # Find data .dat file in rawfiles folder
    # Filename format: SCD_RUN#####_BEAM_YYYYMMDD_HHMMSS.dat
    # where ##### is the data run number 0 padded to 5 digits
    search_string = 'SCD_RUN' + str(datarun).zfill(5) + '_BEAM_'

    datafile = None
    for filename in os.listdir(rawfile_folder):
        if filename.startswith(search_string):
            datafile = filename
            break
    if not datafile:
        return (datarun, False, f"Data file not found for run {datarun}")

    input_file = os.path.join(rawfile_folder, datafile)
    output_file = os.path.join(root_folder, f'run{str(datarun).zfill(5)}.root')

    cmd = ['./PAPERO_convert', input_file, output_file]

    res = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if res.returncode == 0:
        return (datarun, True, "OK")
    return (datarun, False, f"Process exited with code {res.returncode}")

def main():
    parser = argparse.ArgumentParser(description='Process PAPERO data files to obtain rootfiles')
    parser.add_argument('--datarun', type=int, help='Data run number')
    parser.add_argument('--nevents', type=int, help='Number of events to process')
    parser.add_argument('--runlist', type=str, help='Runlist file')
    parser.add_argument('--threads', type=int, default=4, help='Number of parallel workers')
    args = parser.parse_args()

    if not args.nevents:
        args.nevents = -1

    if not args.runlist and not args.datarun:
        print('Please provide data run number (or runlist file)')
        parser.print_help()
        sys.exit(1)

    # Single run processing
    if args.datarun and not args.runlist:
        run, ok, msg = process_data(args.datarun, args.nevents)
        print(f"Run {run}: {msg}")
        return

    # Runlist batch processing
    runs = []
    with open(args.runlist, 'r') as f:
        for line in f:
            line = line.strip()
            if "#" in line or not line:
                continue
            calrun, datarun = line.split()
            runs.append((int(calrun), int(datarun)))

    print(f'\nProcessing runlist file {args.runlist}')

    failed_runs = []
    with ThreadPoolExecutor(max_workers=args.threads) as executor:
        futures = {
            executor.submit(process_data, datarun, args.nevents): datarun
            for calrun, datarun in runs
        }

        with tqdm(total=len(runs), desc="Converting runs", unit="run") as pbar:
            for future in as_completed(futures):
                datarun, success, msg = future.result()
                if not success:
                    failed_runs.append((datarun, msg))
                pbar.update(1)

    if failed_runs:
        print("\nFailures encountered:")
        for datarun, reason in failed_runs:
            print(f"  - Run {datarun}: {reason}")

if __name__ == '__main__':
    main()