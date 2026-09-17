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

bool seek_file_header(std::fstream &file, std::streampos offset, int verbose);

std::tuple<bool, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, std::vector<uint16_t>, std::streampos> read_file_header(std::fstream &file, std::streampos offset, int verbose);

std::streampos seek_first_evt_header(std::fstream &file, std::streampos offset, int verbose);

std::streampos seek_last_evt_header(std::fstream &file, int verbose);

std::tuple<bool, timespec, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, std::streampos> read_evt_header(std::fstream &file, std::streampos offset, int verbose);

bool read_de10_footer(std::fstream &file, std::streampos offset, int verbose);

std::tuple<bool, uint32_t, uint32_t, uint32_t, uint32_t, uint64_t, uint64_t, uint32_t, std::streampos> read_de10_header(std::fstream &file, std::streampos offset, int verbose);

std::vector<uint32_t> read_eventMazinga(std::fstream &file, std::streampos offset, int event_size, int verbose);

#endif