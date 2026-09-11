import sys
import os
import subprocess

rawfile_folder = '../CernBox/CRSPACE/'

def read_bias_from_file(filename):
    # Launch HEF_convert to read bias voltages
    command = f'./HEF_convert --print_bias {filename}'

    # Read output with subprocess
    proc = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    
    # Read bias of one HEF
    output = proc.stdout.readlines()

    # Check if there is an error
    if any("ERROR" in item.decode('utf-8') for item in output):
        print(f'ERROR: reading bias voltages from {filename}')
        return None

    return output[5].decode('utf-8')[15:24]

def main():
    print('Reading bias voltages')

    # List all the files in the raw folder
    files = os.listdir(rawfile_folder)
    # Filter out the files that do not end with .dat
    dat_files = [file for file in files if file.endswith('.dat')]
    # Sort the files in ascending order
    dat_files.sort()

    output_filename = 'bias_voltages.csv'

    with open(output_filename, 'w') as f:
        f.write('#FILENAME\tBIAS VOLTAGE\n')
        for file in dat_files:
            print(f'Reading {file}')
            bias = read_bias_from_file(os.path.join(rawfile_folder, file))
            f.write(f'{file}\t{bias}\n')

if __name__ == '__main__':
    main()