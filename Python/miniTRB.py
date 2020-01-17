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
                    bdata.append(struct.unpack('<H', bytes)[0])  # 2bytes
                else:
                    bdata.append(struct.unpack('>H', bytes)[0])  # 2bytes

            cnt += 1
    hdata = [hex(x) for x in bdata]
    return bdata, hdata


def seek_header(filename):
    offset = 0
    with open(filename, 'rb') as f:
        bdata = []
        while True:
            bytes = f.read(2)
            if bytes == b'':
                break
            elif bytes == b'\x90\xeb' or bytes == b'\xeb\x90':
                break
            else:
                offset += 2
    return offset


def seek_endianess(filename):
    print("Reading data file {0}".format(filename))
    endianess = 0
    offset = 0
    with open(filename, 'rb') as f:
        bdata = []
        while True:
            bytes = f.read(2)
            if bytes == b'':
                break
            elif bytes == b'\xaa\xbb':
                endianess = "big"
                print("File endianess is 'Big Endian'")
                break
            elif bytes == b'\xbb\xaa':
                endianess = "little"
                print("File endianess is 'Little Endian'")
                break
            else:
                offset += 2
    return endianess, offset


def check_kind(filename, offset):
    kind = 0
    with open(filename, 'rb') as f:
        f.seek(offset, 0)
        bytes = f.read(1)
        if bytes == b'\xa0':
            kind = "raw"
        else:
            kind = "compressed"
    return kind

def common_noise(data, event, chip, type=0):
    #Common noise calculation
    values = data[event][chip*64:(chip+1)*64]
    mean = np.mean(values)
    std  = np.std(values)

    if type == 0:
        new_values_fast = values[(values < mean + std) & (values > mean - std)]
        fast_cn = np.mean(new_values_fast)
        return fast_cn
    elif type == 1:
        new_values_fixed = values[values < 50]
        fixed_cn = np.mean(new_values_fixed)
        return fixed_cn
    elif type == 2:
        hard_cm = np.mean(values[8:23][values[8:23] < 50])
        new_values_self = values[24:55][(values[24:55] < hard_cm + 10) & (values[24:55] > hard_cm - 10)]
        self_cn = np.mean(new_values_self)
        return self_cn

def common_noise_event(data, event, type=0):
    cn_event = np.array([], dtype=float)
    for chip in range(int(len(data[event])/64)):
        cn_event = np.append(cn_event, np.full((64,), common_noise(data, event, chip, type)))

    return cn_event
    
def convert(path, cal_file, data_file, signal=True, binary=False):
    print('Entering conversion loop ...')
    evt = 0
    good = True

    #Load calibration file
    cal = cread(path + cal_file)

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

    #Read version
    _, version = bread(path + data_file, header_offset + endianess_offset - 2,
                       1, endianess)
    if version[0] == "0x1212":
        print("File from miniTRB 6VA version")
        miniTRB_channels = 384
    elif version[0] == "0x1313":
        print("File from miniTRB 10VA version")
        miniTRB_channels = 640
    else:
        print("Error: invalid miniTRB version")
        return

    if not (binary):
        out = open(path + "txt/" + data_file + ".csv", 'w')
    else:
        out = path + "bin/" + data_file + ".npy"

    if signal:
        if not binary:
            print('Saving data to file {0} as reduced signal value'.format(path + "txt/" + data_file + ".csv"))
        else:
            print('Saving data to file {0} as reduced signal value'.format(path + "txt/" + data_file + ".npy"))
    else:
        if not binary:
            print('Saving data to file {0} as raw signal value'.format(path + "txt/" + data_file + ".csv"))
        else:
            print('Saving data to file {0} as raw signal value'.format(path + "txt/" + data_file + ".npy"))
 
    while good:
        bdata, hdata = bread(path + data_file, evt * 1024 + header_offset + 4,
                             miniTRB_channels, endianess)
        raw_values = np.array(bdata)
        if raw_values.shape[0] == miniTRB_channels:
            if signal:
                raw_values = np.concatenate(
                    (raw_values[::2], raw_values[1::2])) - cal[3].to_numpy()
                raw_values[cal[6].to_numpy() > 0] = 0
                if not (binary):
                    np.savetxt(out,
                               raw_values.reshape(
                                   1, miniTRB_channels).astype(float),
                               fmt='%1.3f',
                               delimiter=',')
                else:
                    save(out,
                         raw_values.reshape(1, miniTRB_channels).astype(float))
                evt += 1
            else:
                raw_values = np.concatenate(
                    (raw_values[::2], raw_values[1::2]))
                raw_values[cal[6].to_numpy() > 0] = 0
                if not (binary):
                    np.savetxt(out,
                               raw_values.reshape(
                                   1, miniTRB_channels).astype(int),
                               fmt='%1.3f',
                               delimiter=',')
                else:
                    save(out,
                         raw_values.reshape(1, miniTRB_channels).astype(int))
                evt += 1
            print("Saving event {0}".format(evt), end='\r')
        else:
            good = False
            break
    if not binary:
        out.close()

    print("Found {0} events".format(evt))


def compress(path, cal_file, data_file, signal):
    print('Entering compression loop ...')
    evt = 0
    good = True
    #out = open(path + "txt/" + data_file + ".csv", 'w')
    data = np.array([], dtype=float)

    #Load calibration file
    cal = cread(path + cal_file)

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

    #Read version
    _, version = bread(path + data_file, header_offset + endianess_offset - 2,
                       1, endianess)
    if version[0] == "0x1212":
        print("File from miniTRB 6VA version")
        miniTRB_channels = 384
    elif version[0] == "0x1313":
        print("File from miniTRB 10VA version")
        miniTRB_channels = 640
    else:
        print("Error: invalid miniTRB version")
        return

    if signal:
        print('Saving data as reduced signal value')
    else:
        print('Saving data as raw signal value')

    while good:
        bdata, hdata = bread(path + data_file, evt * 1024 + header_offset + 4,
                             miniTRB_channels, endianess)
        raw_values = np.array(bdata)
        if raw_values.shape[0] == miniTRB_channels:
            if signal:
                raw_values = np.concatenate(
                    (raw_values[::2], raw_values[1::2])) - cal[3].to_numpy()
                raw_values[cal[6].to_numpy() > 0] = 0
                data = np.append(
                    data,
                    raw_values.reshape(1, miniTRB_channels).astype(float))
                #np.savetxt(out,raw_values.reshape(1, miniTRB_channels).astype(float), fmt='%1.3f', delimiter=',')
                evt += 1
            else:
                raw_values = np.concatenate(
                    (raw_values[::2], raw_values[1::2]))
                raw_values[cal[6].to_numpy() > 0] = 0
                data = np.append(
                    data,
                    raw_values.reshape(1, miniTRB_channels).astype(float))
                #np.savetxt(out,raw_values.reshape(1, miniTRB_channels).astype(int), fmt='%i', delimiter=',')
                evt += 1
            print("Saving event {0}".format(evt), end='\r')
        else:
            good = False
            break
    print("Found {0} events".format(evt))
    return data.reshape(evt,miniTRB_channels)


def cread(filename):
    with open(filename, 'rb') as f:
        print("Reading calibration file {0}".format(filename))
        cal = pd.read_csv(filename, skiprows=18, header=None)
    return cal


def clusterize(data, highThresh, lowThresh, symmetric=False,
               symmetric_width=1, cn_type=0, max_cn=999):

    maxClusters = 10
    col_names = ['evt', 'nclust', 'seed', 'signal', 'width', 'address', 'cog']
    CLUSTERS = pd.DataFrame(columns=col_names)
    CLUSTERS

    for cnt, event in enumerate(data):
        print("Clustering event {0}".format(cnt + 1), end='\r')

        if cn_type >= 0:
            cn = common_noise_event(data, cnt, cn_type)
            event = event - cn
        
        seeds = np.where(
            event > highThresh)[0]  # candidate seed strips over high threshold

        if np.size(seeds) > maxClusters or np.size(seeds) == 0:
            continue

        seeds = np.delete(
            seeds,
            np.where(np.diff(seeds) == 1)[0]
        )  # remove adjacent strips over threshold (to prevent counting same cluster 2 times)
        if not (np.size(seeds) == 0):
            nclust = np.size(seeds)
            for seed in np.nditer(seeds):
                if symmetric == True:
                    if int(seed) - symmetric_width >= 0 and int(
                            seed) + symmetric_width < len(event):
                        seed = int(seed)
                        signal = np.sum(event[seed - symmetric_width:seed +
                                              symmetric_width])
                        width = 2 * symmetric_width + 1
                        cogN = np.sum(event[seed - symmetric_width:seed +
                                            symmetric_width] *
                                      np.arange(seed - symmetric_width,
                                                seed + symmetric_width))
                        cogD = np.sum(event[seed - symmetric_width:seed +
                                            symmetric_width])
                        address = seed

                        CLUSTERS = CLUSTERS.append(dict(evt=int(cnt + 1),
                                                        nclust=int(nclust),
                                                        seed=int(seed),
                                                        signal=signal,
                                                        width=int(width),
                                                        address=int(address),
                                                        cog=(cogN / cogD)),
                                                   ignore_index=True)
                    else:
                        continue
                else:
                    seed = int(seed)
                    signal = event[seed]
                    width = 1
                    cogN = seed * event[seed]
                    cogD = event[seed]
                    address = seed
                    L = 0
                    R = 0

                    overThreshL = True
                    overThreshR = True

                    while overThreshL:
                        if (seed - L - 1) > 0:
                            if event[seed - L - 1] > lowThresh and event[
                                    seed - L - 1] < highThresh:
                                signal += event[seed - L - 1]
                                cogN += (seed - L - 1) * event[seed - L - 1]
                                cogD += event[seed - L - 1]
                                address = (seed - L - 1)
                                width += 1
                                L += 1
                            else:
                                overThreshL = False
                        else:
                            overThreshL = False


                    while overThreshR:
                        if (seed + R + 1) < len(event):
                            if event[seed + R + 1] > lowThresh and event[
                                    seed + R + 1] > highThresh:
                                signal += event[seed + R + 1]
                                cogN += (seed + R + 1) * event[seed + L + 1]
                                cogD += event[seed + L + 1]
                                R += 1
                                width += 1
                            else:
                                overThreshR = False
                        else:
                            overThreshR = False


                    CLUSTERS = CLUSTERS.append(dict(evt=int(cnt + 1),
                                                    nclust=int(nclust),
                                                    seed=int(seed),
                                                    signal=signal,
                                                    width=int(width),
                                                    address=int(address),
                                                    cog=(cogN / cogD)),
                                               ignore_index=True)
        else:
            continue

    return CLUSTERS

