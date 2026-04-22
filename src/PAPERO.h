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

template <typename T>
void print(std::vector<T> const &v)
{
    for (auto i : v)
        std::cout << std::hex << i << ' ' << std::endl;
    std::cout << '\n';
}

template <typename T>
std::vector<T> reorder(std::vector<T> const &v)
{
    int nCH = 32;
    std::vector<T> reordered_vec(v.size());
    int j = 0;
    std::vector<int> order = {1, 0};
    for (int ch = 0; ch < nCH; ch++)
        for (int adc = 0; adc < order.size(); adc++)
            reordered_vec.at(order.at(adc) * nCH + ch) = v.at(j++);

    std::vector<bool> mirror = {false, true};
    for (int adc = 0; adc < order.size(); adc++)
        if (mirror.at(adc))
            std::reverse(reordered_vec.begin() + (adc * nCH), reordered_vec.begin() + ((adc + 1) * nCH));

    return reordered_vec;
}

unsigned int gray_to_uint(unsigned int g, unsigned int bits);

uint64_t seek_first_evt_header(std::fstream &file, uint64_t offset, bool verbose);

bool read_evt_header(std::fstream &file, uint64_t offset, bool verbose);

bool read_de10_footer(std::fstream &file, uint64_t offset, bool verbose);

std::tuple<bool, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, uint64_t> read_de10_header(std::fstream &file, uint64_t offset, bool verbose);

std::vector<unsigned int> read_event(std::fstream &file, uint64_t offset, int event_size, bool verbose);

std::vector<unsigned int> read_internalADC_event(std::fstream &file, uint64_t offset, int event_size, bool verbose);

#endif