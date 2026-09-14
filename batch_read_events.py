import os
import re
import subprocess

rawfile_folder = '../CernBox/CRSPACE/'

def read_events_from_file(filename):
    # Launch HEF_convert to read bias voltages
    command = f'./HEF_convert --find_events {filename}'

    # Read output with subprocess
    proc = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = proc.stdout.readlines()

    # Check if there is an error
    if any("ERROR" in item.decode('utf-8') for item in output):
        print(f'ERROR: reading number of events from {filename}')
        return None

    # Number of events is digits between 'Expecting' and 'events'
    number_of_events = re.search(r'Expecting (\d+) events', output[5].decode('utf-8'))
    if not number_of_events:
        print(f'ERROR: reading number of events from {filename}')
        return None
    return int(number_of_events.group(1))

def main():
    print('Reading number of events')

    # List all the files in the raw folder
    files = os.listdir(rawfile_folder)
    # Filter out the files that do not end with .dat
    dat_files = [file for file in files if file.endswith('.dat')]
    # Sort the files in ascending order
    dat_files.sort()

    output_filename = 'events_in_file.csv'

    with open(output_filename, 'w') as f:
        f.write('#FILENAME\tNUMBER OF EVENTS\n')
        for file in dat_files:
            print(f'Reading {file}')
            events = read_events_from_file(os.path.join(rawfile_folder, file))
            f.write(f'{file}\t{events}\n')

if __name__ == '__main__':
    main()