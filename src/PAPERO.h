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

unsigned int gray_to_uint(unsigned int g, unsigned int bits);

uint64_t seek_first_evt_header(std::fstream &file, uint64_t offset, bool verbose);

bool read_evt_header(std::fstream &file, uint64_t offset, bool verbose);

bool read_de10_footer(std::fstream &file, uint64_t offset, bool verbose);

std::tuple<bool, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, uint64_t> read_de10_header(std::fstream &file, uint64_t offset, bool verbose);

std::vector<unsigned int> read_event(std::fstream &file, uint64_t offset, int event_size, bool verbose);

std::vector<unsigned int> read_internalADC_event(std::fstream &file, uint64_t offset, int event_size, bool verbose);

#endif