#ifndef PAPERO_GUI_HH
#define PAPERO_GUI_HH

#include "TMath.h"
#include "TROOT.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <tuple>
#include <unistd.h>
#include <iostream>
#include <time.h>

// for conversion of PAPERO DAQ raw files to a rootfile with TTrees of raw events

template <typename T>
void print(std::vector<T> const &v)
{
    for (auto i : v)
    {
        std::cout << std::hex << i << ' ' << std::endl;
    }
    std::cout << '\n';
}

template <typename T>
std::vector<T> reorder(std::vector<T> const &v)
{
    std::vector<T> reordered_vec(v.size());
    int j = 0;
    constexpr int order[] = {12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1};
    for (int ch = 0; ch < 128; ch++)
    {
        for (int adc : order)
        {
            int write_idx = (adc * 128 + ch + 896) % 1792; // Swap GPIO connectors to match physical layout
            reordered_vec.at(write_idx) = v.at(j);
            j++;
        }
    }
    return reordered_vec;
}

bool seek_file_header(std::fstream &file, std::streampos offset, int verbose);

std::tuple<bool, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, std::vector<uint16_t>, std::streampos> read_file_header(std::fstream &file, std::streampos offset, int verbose);

std::streampos seek_first_evt_header(std::fstream &file, std::streampos offset, int verbose);

std::streampos seek_last_evt_header(std::fstream &file, int verbose);

bool read_old_evt_header(std::fstream &file, std::streampos offset, int verbose);
std::tuple<bool, timespec, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, std::streampos> read_evt_header(std::fstream &file, std::streampos offset, int verbose);

bool read_de10_footer(std::fstream &file, std::streampos offset, int verbose);

std::tuple<bool, uint32_t, uint32_t, uint32_t, uint32_t, uint64_t, uint64_t, uint32_t, Float_t, Float_t, Float_t, std::streampos> read_de10_header(std::fstream &file, std::streampos offset, int verbose);

std::vector<uint32_t> read_eventHEF(std::fstream &file, std::streampos offset, int event_size, int verbose);

std::vector<std::pair<Float_t, Float_t>> read_bias_voltages(std::fstream &file, std::streampos offset, int verbose);

#endif