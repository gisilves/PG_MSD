#include <iostream>
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TH1.h"
#include "TGraph.h"
#include <ctime>
#include <tuple>
#include <CLI/CLI.hpp>

#include "PAPERO.h"

#define max_detectors 8
#define BUFFER_SIZE 1048576 // 1MB buffer

#include <iostream>

void display_progress(int current_event, int expected_events, int width = 50)
{
    static bool first_run = true;

    if (!first_run)
    {
        std::cout << "\033[A\r\033[2K"; // Move up, go to start, clear line 1
        std::cout << "\033[2K";         // Clear line 2
    }
    else
    {
        first_run = false;
    }

    // Line 1: Status message
    std::cout << "\tReading event " << current_event << " / " << expected_events << "\n";

    // Line 2: Progress bar
    float progress = static_cast<float>(current_event) / static_cast<float>(expected_events);
    int pos = static_cast<int>(width * progress);

    std::cout << "\t[";
    for (int i = 0; i < width; ++i)
    {
        if (i < pos)
            std::cout << "=";
        else if (i == pos)
            std::cout << ">";
        else
            std::cout << " ";
    }
    std::cout << "] " << static_cast<int>(progress * 100.0) << "%";
    std::cout.flush();
}

int main(int argc, char *argv[])
{
    CLI::App app{"miniMazinga_convert"};

    bool verbose = false;
    bool silent = false;
    bool gsi = false;
    bool find_events = false;
    int boards = 0;
    int nevents = -1;
    std::string input_file;
    std::string output_file_str;
    std::ofstream output_file;

    app.add_flag("-v,--verbose", verbose, "Verbose output");
    app.add_flag("--silent", silent, "Silent mode");
    app.add_flag("--find_events", find_events, "Find number of events in the file");
    app.add_option("--nevents", nevents, "Number of events to be read");
    app.add_option("raw_data_file", input_file, "Raw data input file")->required();
    app.add_option("output_file", output_file_str, "Output file");

    try
    {
        CLI11_PARSE(app, argc, argv);
        if (!find_events && output_file_str.empty())
        {
            std::cout << "ERROR: output file is required" << std::endl;
            return 1;
        }
    }
    catch (const CLI::ParseError &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    TFile *foutput;

    // Open binary data file
    std::fstream file(input_file.c_str(), std::ios::in | std::ios::out | std::ios::binary);
    if (file.fail())
    {
        if (!silent)
            std::cout << "ERROR: can't open input file" << std::endl; // file could not be opened
        return 2;
    }

    // Disable stdio sync and add large buffer
    std::ios::sync_with_stdio(false);
    char file_buffer[BUFFER_SIZE];
    file.rdbuf()->pubsetbuf(file_buffer, BUFFER_SIZE);

    if (!silent)
    {
        std::cout << " " << std::endl;
        std::cout << "Processing file " << input_file.c_str() << std::endl;
    }

    // Create output csv file if required
    if (!output_file_str.empty())
    {
        output_file.open(output_file_str);
        // Add a header line: # TIMESTAMP, EVENT_ID, BOARD_ID, RAW WORD
        output_file << "# TIMESTAMP, EVENT_ID, BOARD_ID, RAW WORD" << std::endl;
    }

    std::vector<uint32_t> raw_event_buffer; // Buffer to store raw event words

    bool is_good = false;
    int evtnum = 0;
    int evt_to_read = -1;
    int expected_events = -1;
    int board_id = -1;
    int trigger_number = -1;
    int trigger_id = -1;
    int evt_size = 0;
    int boards_read = 0;
    uint32_t fw_version = 0;
    uint64_t int_timestamp = 0;
    uint64_t ext_timestamp = 0;
    
    std::streampos evt_offset(0);
    std::streampos last_evt_offset(0);
    std::streampos old_offset(0);
    std::streampos padding_offset(0);

    char dummy[100];
    float mean_rate = 0;

    // Map detector_ids
    std::map<uint16_t, int> detector_ids_map;
    std::vector<uint16_t> detector_ids;
    
    // Tuples to store headers values
    std::tuple<bool, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, std::vector<uint16_t>, std::streampos> file_retValues;
    std::tuple<bool, uint32_t, uint32_t, uint32_t, uint32_t, uint64_t, uint64_t, uint32_t, std::streampos> de10_retValues;
    std::tuple<bool, timespec, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, std::streampos> maka_retValues;

    // Seek new format file header
    bool new_format = seek_file_header(file, evt_offset, verbose);

    if (new_format)
    {
        if (!silent)
            std::cout << "New data format" << std::endl;
        file_retValues = read_file_header(file, evt_offset, verbose); // read file header
        is_good = std::get<0>(file_retValues); // check if file header is good
        boards = std::get<5>(file_retValues); // get number of boards

        // map detector_ids values to progressive number from 0 to size of detector_ids
        detector_ids = std::get<6>(file_retValues);
        for (size_t i = 0; i < detector_ids.size(); i++)
        {
            detector_ids_map[detector_ids.at(i)] = i;
        }

        old_offset = std::get<7>(file_retValues); // get file header offset
        evt_offset = seek_first_evt_header(file, old_offset, verbose); // seek first evt header
        if (evt_offset != old_offset)
        {
            if (!silent)
                std::cout << "WARNING: first evt header has a " << evt_offset - old_offset << " delta value " << std::endl;
        }

        // Search for last evt header
        if (!silent)
            std::cout << "\nSearching for last evt header" << std::endl;
        last_evt_offset = seek_last_evt_header(file, verbose); // seek last evt header

        maka_retValues = read_evt_header(file, last_evt_offset, verbose); // read last evt header
        if (std::get<0>(maka_retValues))
        {
            expected_events = std::get<3>(maka_retValues); // expected events is the evt_number of the last event
        }

        if (!silent)
            std::cout << "\tExpecting " << std::dec<< expected_events << " events in the file\n" << std::endl;

        // Go back to the first evt header
        file.seekg(evt_offset);

        if (find_events) // if find_events is true, we need to close the file and exit, no need to read events
        {
            // Close files and exit
            if (!output_file_str.empty())
                output_file.close();
            file.close();
            return 0;
        }
    }
    else
    {
        std::cerr << "ERROR: data can only be of new format type, check file" << std::endl;
        return 2;
    }

    if (nevents > 0)
    {
        evt_to_read = nevents;
        if (!silent)
            std::cout << "\tReading " << evt_to_read << " events" << std::endl;
    }

    while (!file.eof())
    {
        if (evtnum == evt_to_read)
        {
            break;
        }

        is_good = false;
        maka_retValues = read_evt_header(file, evt_offset, verbose);
        if (std::get<0>(maka_retValues))
        {
            evt_offset = std::get<7>(maka_retValues);
            for (size_t de10 = 0; de10 < std::get<4>(maka_retValues); de10++)
            {
                de10_retValues = read_de10_header(file, evt_offset, verbose); // read de10 header
                is_good = std::get<0>(de10_retValues);

                if (is_good)
                {
                    boards_read++;
                    evt_size = std::get<1>(de10_retValues);
                    evt_size = evt_size - 2; // TODO: check why we need to substract 2 bytes (fw writes wrong evt size?)

                    fw_version = std::get<2>(de10_retValues);
                    trigger_number = std::get<3>(de10_retValues);
                    board_id = std::get<4>(de10_retValues);
                    int_timestamp = std::get<5>(de10_retValues);
                    ext_timestamp = std::get<6>(de10_retValues);
                    trigger_id = std::get<7>(de10_retValues);
                    evt_offset = std::get<8>(de10_retValues);

                    if (!silent)
                    {
                        // std::cout << "\r\tReading event " << evtnum << std::flush;
                        display_progress(evtnum, expected_events);
                    }

                    if (verbose == 1 && !silent)
                    {
                        std::cout << "\tBoard ID " << board_id << std::endl;
                        std::cout << "\tBoards read " << boards_read << " out of " << boards << std::endl;
                        std::cout << "\tTrigger ID " << trigger_id << std::endl;
                        std::cout << "\tFW version is: " << std::hex << fw_version << std::dec << std::endl;
                        std::cout << "\tEvt lenght: " << evt_size << std::endl;
                        std::cout << "\tInternal timestamp: " << int_timestamp << std::endl;
                        std::cout << "\tExternal timestamp: " << ext_timestamp << std::endl;

                        std::cout << "\tOffset (computed): " << evt_offset << std::endl;
                        std::cout << "\tOffset (file): " << file.tellg() << std::endl;
                    }

                    padding_offset = 0;
                    raw_event_buffer = std::move(read_eventMazinga(file, evt_offset, evt_size, verbose));

                    int det_idx = detector_ids_map.at(board_id);

                    output_file << int_timestamp << ", " << evtnum << ", " << board_id << ", ";
                    for (uint32_t word : raw_event_buffer)
                    {
                        output_file << word << ", ";
                    }
                    output_file << std::endl;

                    evt_offset += std::streamoff(static_cast<int64_t>(evt_size) * 4 + 8 + 44); // 8 is the size of the de10 footer + crc, 44 is the size of the de10 header
                }
            }
            boards_read = 0;
            evtnum++;
        }
        else
        {
            if (verbose == 1)
                std::cout << "\nReached EOF at offset " << evt_offset << std::endl;
            break;
        }
    }

    output_file.close();
    foutput->Close();
    file.close();
    return 0;
}