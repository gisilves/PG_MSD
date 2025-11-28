#include <iostream>
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TH1.h"
#include "TGraph.h"
#include "anyoption.h"
#include <ctime>
#include <tuple>

#include "CLI.hpp"
#include "PAPERO.h"

int main(int argc, char *argv[])
{
    CLI::App app{"PAPERO_i2c"};

    bool verbose = false;
    int boards = 0;
    int nevents = -1;
    std::string input_file;
    std::string output_file;

    app.add_option("raw_data_file", input_file, "Raw data input file")->required();
    app.add_option("output_rootfile", output_file, "Output ROOT file")->required();
    app.add_flag("-v,--verbose", verbose, "Verbose output");
    app.add_option("--boards", boards, "Number of DE10Nano boards connected (for old data format)");
    app.add_option("--nevents", nevents, "Number of events to be read ");

    CLI11_PARSE(app, argc, argv);

    TFile *foutput;

    // Open binary data file
    std::fstream file(input_file.c_str(), std::ios::in | std::ios::out | std::ios::binary);
    if (file.fail())
    {
        std::cout << "ERROR: can't open input file" << std::endl; // file could not be opened
        return 2;
    }

    std::cout << " " << std::endl;
    std::cout << "Processing file " << input_file.c_str() << std::endl;

    // Find if there is an offset before file header
    bool is_good = false;
    bool gsi = false;
    int evtnum = 0;
    int evt_to_read = -1;
    int board_id = -1;
    int trigger_number = -1;
    int trigger_id = -1;
    int evt_size = 0;
    int boards_read = 0;
    uint32_t offset = 0;
    uint32_t fw_version = 0;
    uint32_t i2cmsg = 0;
    uint32_t ext_timestamp = 0;
    uint32_t old_offset = 0;
    int padding_offset = 0;
    char dummy[100];
    float mean_rate = 0;

    bool is_new_format = false;
    std::map<uint16_t, int> detector_ids_map;

    std::vector<uint16_t> detector_ids;
    std::tuple<bool, uint32_t, uint32_t, uint8_t, uint16_t, uint16_t, std::vector<uint16_t>, uint32_t> file_retValues;
    std::tuple<bool, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int> de10_retValues;
    std::tuple<bool, timespec, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint32_t> maka_retValues;

    bool new_format = seek_file_header(file, offset, false);

    if (new_format)
    {
        is_new_format = true;
        std::cout << "New data format" << std::endl;
        file_retValues = read_file_header(file, offset, false);
        is_good = std::get<0>(file_retValues);
        boards = std::get<5>(file_retValues);

        // map detector_ids values to progressive number from 0 to size of detector_ids
        detector_ids = std::get<6>(file_retValues);
        for (size_t i = 0; i < detector_ids.size(); i++)
        {
            detector_ids_map[detector_ids.at(i)] = i;
        }

        old_offset = std::get<7>(file_retValues);
        offset = seek_first_evt_header(file, old_offset, false);
        if (offset != old_offset)
        {
            std::cout << "WARNING: first evt header has a " << offset - old_offset << " delta value " << std::endl;
        }
    }
    else
    {
        std::cout << "No file header: assuming old data format" << std::endl;
        is_new_format = false;
        offset = 0;
    }

    if (!is_new_format)
    {
        if (boards == 0)
        {
            std::cout << "ERROR: you need to provide the number of boards connected" << std::endl;
            return 2;
        }
        offset = seek_first_evt_header(file, 0, false);
    }

    if (nevents > 0)
    {
        evt_to_read = nevents;
        std::cout << "\tReading " << evt_to_read << " events" << std::endl;
    }

    while (!file.eof())
    {   
        if (evtnum == evt_to_read)
        {
            break;
        }

        is_good = false;
        maka_retValues = read_evt_header(file, offset, false);
        if (std::get<0>(maka_retValues))
        {
            std::cout << "\r\tReading event " << std::dec << evtnum << std::endl;
            offset = std::get<7>(maka_retValues);
            for (size_t de10 = 0; de10 < std::get<4>(maka_retValues); de10++)
            {
                std::cout << "\t\tReading board " << de10 << std::endl;
                de10_retValues = read_de10_header(file, offset, 3); // read de10 header
                is_good = std::get<0>(de10_retValues);

                if (is_good)
                {
                    boards_read++;
                    evt_size = std::get<1>(de10_retValues);
                    fw_version = std::get<2>(de10_retValues);
                    trigger_number = std::get<3>(de10_retValues);
                    board_id = std::get<4>(de10_retValues);
                    i2cmsg = std::get<5>(de10_retValues);
                    ext_timestamp = std::get<6>(de10_retValues);
                    trigger_id = std::get<7>(de10_retValues);
                    offset = std::get<8>(de10_retValues);

                    offset += evt_size * 4 + 8 + 36; // 8 is the size of the de10 footer + crc, 36 is the size of the de10 header
                }
            }
            boards_read = 0;
            
            std::cin.ignore();
            evtnum++;
        }
        else
        {
            break;
        }
    }

    std::cout << "\n\tClosing file after " << std::dec << evtnum << " events" << std::endl;

    file.close();
    return 0;
}
