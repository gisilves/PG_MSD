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

// for conversion with PAPERO_compress of FOOT PAPERO DAQ raw files to a rootfile with TTrees of raw events

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
    constexpr int order[] = {1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12};
    for (int ch = 0; ch < 128; ch++)
    {
        for (int adc : order)
        {
            reordered_vec.at(adc * 128 + ch) = v.at(j);
            j++;
        }
    }
    return reordered_vec;
}

bool seek_file_header(std::fstream &file, uint32_t offset, int verbose);

std::tuple<bool, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, std::vector<uint16_t>, uint32_t> read_file_header(std::fstream &file, uint32_t offset, int verbose);

int seek_first_evt_header(std::fstream &file, uint32_t offset, int verbose);

bool read_old_evt_header(std::fstream &file, uint32_t offset, int verbose);
std::tuple<bool, timespec, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint32_t> read_evt_header(std::fstream &file, uint32_t offset, int verbose);

bool read_de10_footer(std::fstream &file, uint32_t offset, int verbose);

std::tuple<bool, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint64_t, uint32_t, int> read_de10_header(std::fstream &file, uint32_t offset, int verbose);

std::vector<uint32_t> read_eventHEF(std::fstream &file, uint32_t offset, int event_size, int verbose);

#endif