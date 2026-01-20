# PG_MSD
Software for Microstrip Silicon Detectors data analysis

###### Root 6 with C++14 support needed (to compile, *make + executable name* or *make all*):

*Common to all branches*

- **raw_clusterize:** to find clusters from compressed data

- **raw_cn:** to perform a CN study

- **raw_threshold_scan:** to perform a scan on threshold values for cluster reconstruction

- **raw_viewer:** to open the GUI viewer for raw data

- **calibration:** to compute calibrations from raw data

*ASTRA branch*

- **ASTRA_convert:** to compress binary raw data into rootfiles

- **ASTRA_info:** to retrieve info from PAPERO event headers

*Common to main and PAPERO branch*

- **PAPERO_convert:** to compress binary raw data into rootfiles

- **PAPERO_info:** to retrieve info from PAPERO event headers

