import sys
import os
import re
import argparse
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from tqdm import tqdm

rawfile_folder = '../CernBox/CRSPACE/'
root_folder = '../CernBox/RootFiles/'

def get_eos_available_space(quota_node="/eos/user/g/gisilves/"):
    output = subprocess.check_output(["eos", "quota", quota_node], text=True)
    match = re.search(r'(\d+(?:\.\d+)?)\s*%', output)
    if not match:
        raise ValueError("Could not parse filled percentage from output.")
    
    filled_percentage = float(match.group(1))
    available_percentage = 100.0 - filled_percentage
    return available_percentage * 10**10

def process_data(datarun, nevents):
    search_string = f'SCD_RUN{str(datarun).zfill(5)}_BEAM_'

    datafile = None
    for filename in os.listdir(rawfile_folder):
        if filename.startswith(search_string):
            datafile = filename
            break
    if not datafile:
        return (datarun, False, "File not found")

    filesize = os.path.getsize(os.path.join(rawfile_folder, datafile))
    if get_eos_available_space() < filesize:
        return (datarun, False, "EOS space insufficient")

    cmd = [
        './HEF_convert',
        os.path.join(rawfile_folder, datafile),
        os.path.join(root_folder, f'run{str(datarun).zfill(5)}.root'),
        '--compression', '5',
        '--nevents', str(nevents),
        '--silent'
    ]

    # DEVNULL prevents interleaved subprocess console output
    res = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if res.returncode == 0:
        return (datarun, True, "OK")
    return (datarun, False, f"Process exited with code {res.returncode}")

def main():
    parser = argparse.ArgumentParser(description='Process HEF data files to obtain rootfiles')
    parser.add_argument('--datarun', type=int, help='Data run number')
    parser.add_argument('--nevents', type=int, default=-1, help='Number of events to process')
    parser.add_argument('--runlist', type=str, help='Runlist file')
    parser.add_argument('--threads', type=int, default=4, help='Number of parallel workers')
    args = parser.parse_args()

    if not args.runlist and not args.datarun:
        print('Please provide data run number (or runlist file)')
        sys.exit(1)

    if args.datarun and not args.runlist:
        run, ok, msg = process_data(args.datarun, args.nevents)
        print(f"Run {run}: {msg}")
        return

    runs = []
    with open(args.runlist, 'r') as f:
        for line in f:
            line = line.strip()
            if "#" in line or not line:
                continue
            _, datarun = line.split()
            runs.append(int(datarun))

    # ThreadPool execution with tqdm update on completion
    failed_runs = []
    with ThreadPoolExecutor(max_workers=args.threads) as executor:
        futures = {executor.submit(process_data, run, args.nevents): run for run in runs}
        
        with tqdm(total=len(runs), desc="Processing runs", unit="run") as pbar:
            for future in as_completed(futures):
                run, success, msg = future.result()
                if not success:
                    failed_runs.append((run, msg))
                pbar.update(1)

    if failed_runs:
        print("\nFailures encountered:")
        for run, reason in failed_runs:
            print(f"  - Run {run}: {reason}")

if __name__ == '__main__':
    main()