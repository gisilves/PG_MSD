import struct
import numpy as np
from numpy import save
import matplotlib.pyplot as plt
from matplotlib.pyplot import figure
import pandas as pd
import os


def bread(filename, position, size, endianess):
    with open(filename, 'rb') as f:
        bdata = []
        cnt = 0
        f.seek(position, 0)

        while cnt < size:
            bytes = f.read(2)
            if bytes == b'':
                break
            else:
                if endianess == "little":
                    bdata.append(struct.unpack('<H', bytes)[0])  # 4bytes
                else:
                    bdata.append(struct.unpack('>H', bytes)[0])  # 4bytes

            cnt += 1
    hdata = [hex(x) for x in bdata]
    bdata = [x/4 for x in bdata]
    return bdata, hdata


def seek_run_header(filename):
    run_offset = 0
    with open(filename, 'rb') as f:
        bdata = []
        while True:
            bytes = f.read(4)
            print(bytes)
            if bytes == b'':
                break
            elif bytes == b'\xaa\xaa\x34\x12' or bytes == b'\x12\x34\xaa\xaa':
                break
            else:
                offset += 4
    return run_offset

def seek_evt_builder_header(filename):
    run_offset = 0
    with open(filename, 'rb') as f:
        bdata = []
        while True:
            bytes = f.read(4)
            print(bytes)
            if bytes == b'':
                break
            elif bytes == b'\xaa\xaa\x34\x12' or bytes == b'\x12\x34\xaa\xaa':
                break
            else:
                offset += 4
    return run_offset

def convert(path, data_file):
    print('Entering conversion loop ...')
    evt = 0
    good = True

    #Check for binary file endianess
    endianess, endianess_offset = seek_endianess(path + data_file)

    #Seek for event header offset
    header_offset = seek_header(path + data_file)

    #Read kind
    kind = check_kind(path + data_file, header_offset + 3)

    if kind == "raw":
        print("File {0} is raw data file".format(path + data_file))
    else:
        print("Error: file is not in raw data format")
        return

    while good:
        bdata, hdata = bread(path + data_file, evt * 1024 + header_offset + 4,
                             miniTRB_channels, endianess)
        raw_values = np.array(bdata)
        
        if raw_values.shape[0] == miniTRB_channels:         
            evt += 1
            print("Saving event {0}".format(evt), end='\r')
        else:
            good = False
            break

    print("Found {0} events".format(evt))