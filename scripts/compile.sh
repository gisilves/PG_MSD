#!/bin/bash

# Compile the code, stopping in case of error

echo "Compiling the code"

cmake ..
# check if cmake was successful
if [ $? -ne 0 ]
then
    echo "CMake failed. Stopping execution."
    exit 0
fi
nproc=$(nproc)

make -j $nproc
# check if make was successful
if [ $? -ne 0 ]
then
    echo "Make failed. Stopping execution."
    exit 0
fi