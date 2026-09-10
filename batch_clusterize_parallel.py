import sys
import os
import argparse
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from tqdm import tqdm

root_folder = '../CernBox/RootFiles/'
cal_folder = '../CernBox/CalFiles/'
clus_folder = '../CernBox/ClusFiles/'

def process_cluster(calrun, datarun):
    input_file = os.path.join(root_folder, f'run{str(datarun).zfill(5)}.root')
    calfile = os.path.join(cal_folder, f'calib_run{str(calrun).zfill(5)}.cal')
    output_file = os.path.join(clus_folder, f'clus_run{str(datarun).zfill(5)}')

    # Check input files exist before launching process
    if not os.path.exists(input_file):
        return (datarun, False, f"Input ROOT file missing: {input_file}")
    if not os.path.exists(calfile):
        return (datarun, False, f"Calibration file missing: {calfile}")

    cmd = [
        './raw_clusterize',
        '--input_files', input_file,
        '--calibration_file', calfile,
        '--output_file', output_file,
        '--silent'
    ]

    res = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if res.returncode == 0:
        return (datarun, True, "OK")
    return (datarun, False, f"Process exited with code {res.returncode}")

def main():
    parser = argparse.ArgumentParser(description='Process raw rootfiles to obtain clusterized files')
    parser.add_argument('--calrun', type=int, help='Calibration run number')
    parser.add_argument('--datarun', type=int, help='Data run number')
    parser.add_argument('--runlist', type=str, help='Runlist file')
    parser.add_argument('--threads', type=int, default=4, help='Number of parallel workers')
    args = parser.parse_args()

    if not args.runlist and not args.datarun:
        print('Please provide data run number (or runlist file)')
        parser.print_help()
        sys.exit(1)

    # Single run processing
    if args.datarun and not args.runlist:
        if args.calrun is None:
            print('Please provide calibration run number (--calrun)')
            sys.exit(1)
        run, ok, msg = process_cluster(args.calrun, args.datarun)
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

    failed_runs = []
    with ThreadPoolExecutor(max_workers=args.threads) as executor:
        futures = {
            executor.submit(process_cluster, calrun, datarun): datarun 
            for calrun, datarun in runs
        }

        with tqdm(total=len(runs), desc="Clusterizing runs", unit="run") as pbar:
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
