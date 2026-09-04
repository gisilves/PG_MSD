import os
import subprocess

cluster_folder = '/mnt/f/CRSPACE/clusfiles/'
output_folder = '/mnt/f/CRSPACE/output/'

def main():
    
    # List all the files in the cluster folder
    files = os.listdir(cluster_folder)
    # Filter out the files that do not end with .root
    root_files = [file for file in files if file.endswith('.root')]
    # Sort the files in ascending order
    root_files.sort()
    
    # For each file in the cluster folder, read the clusters 
    # example command to run: root -b -q -l "read_clusters.C(\"/mnt/f/CRSPACE/ClusFiles/clus_run00073.root\",\"/mnt/f/CRSPACE/output/clus_run00073.root\", 0)"
    
    
    
    for file in root_files:
        print(f"Processing file {file}")
        
        input_file = f"{cluster_folder}{file}"
        output_file = f"{output_folder}{file[:-5]}" # remove the .root extension for the output file
        
        for board in range(8):
            command = f'root -b -q -l "read_clusters.C(\\"{input_file}\\", \\"{output_file}\\")"'
            print(f"Running command: {command}")
            subprocess.run(command, shell=True)
if __name__ == '__main__':
    main()