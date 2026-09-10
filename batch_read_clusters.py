import os
import subprocess

cluster_folder = '../CernBox/ClusFiles/'
calib_folder = '../CernBox/CalFiles/'
output_folder = '../CernBox/Output/'

def main():
    
    # List all the files in the cluster folder
    files = os.listdir(cluster_folder)
    # Filter out the files that do not end with .root
    root_files = [file for file in files if file.endswith('.root')]
    # Sort the files in ascending order
    root_files.sort()

    # List all the files in the calibration folder
    files = os.listdir(calib_folder)
    # Filter out the files that do not end with .cal
    cal_files = [file for file in files if file.endswith('.cal')]
    # Sort the files in ascending order
    cal_files.sort()

    print(f"Found {len(root_files)} files in the cluster folder")
    print(f"Found {len(cal_files)} files in the calibration folder")

    print('\nProcessing runlist file runlist.txt')
            
    runs = []
    with open("runlist.txt", 'r') as f:
        for line in f:
            line = line.strip()
            if "#" in line:
                continue
            if line:
                calrun, datarun = line.split()
                runs.append((int(calrun), int(datarun)))

    for i, (calrun, datarun) in enumerate(runs, 1):
        # Find the correspondinf cluster file
        clusfile = f"clus_run{datarun:05d}.root"
        if clusfile not in root_files:
            print(f"Cluster file {clusfile} not found in the cluster folder")
            continue

        # Find the corresponding calibration file
        calfile = f"calib_run{calrun:05d}.cal"
        if calfile not in cal_files:
            print(f"Calibration file {calfile} not found in the calibration folder")
            continue

        # For each file in the cluster folder, read the clusters 
        # example command to run: root -b -q -l "read_clusters.C(\"/mnt/f/CRSPACE/ClusFiles/clus_run00073.root\",\"/mnt/f/CRSPACE/output/clus_run00073.root\", 0)"
        
        input_file = f"{cluster_folder}{clusfile}"
        output_file = f"{output_folder}processed_{clusfile}"

        command = f'root -b -q -l "read_clusters.C(\\"{input_file}\\", \\"{output_file}\\", \\"{calib_folder}{calfile}\\")"'
        print(f"Running command: {command}")
        subprocess.run(command, shell=True)

if __name__ == '__main__':
    main()