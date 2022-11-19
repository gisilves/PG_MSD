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

bool seek_file_header(std::fstream &file, uint32_t offset, bool verbose);

std::tuple<bool, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, std::vector<uint16_t>, uint32_t> read_file_header(std::fstream &file, uint32_t offset, bool verbose);

int seek_first_evt_header(std::fstream &file, uint32_t offset, bool verbose);

bool read_old_evt_header(std::fstream &file, uint32_t offset, bool verbose);
std::tuple<bool, time_t, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint32_t> read_evt_header(std::fstream &file, uint32_t offset, bool verbose);

bool read_de10_footer(std::fstream &file, uint32_t offset, bool verbose);

std::tuple<bool, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int> read_de10_header(std::fstream &file, uint32_t offset, bool verbose);

std::vector<uint32_t> read_event(std::fstream &file, uint32_t offset, int event_size, bool verbose, bool astra);

#endif