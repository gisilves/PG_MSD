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
    CLI::App app{"HEF_convert"};

    bool verbose = false;
    bool silent = false;
    bool gsi = false;
    bool find_events = false;
    int boards = 0;
    int nevents = -1;
    int compression = 0;
    std::string input_file;
    std::string output_file;

    app.add_flag("-v,--verbose", verbose, "Verbose output");
    app.add_flag("--silent", silent, "Silent mode");
    app.add_flag("--find_events", find_events, "Find number of events in the file");
    app.add_option("--nevents", nevents, "Number of events to be read");
    app.add_option("--compression", compression, "Compression level (0-9)");
    app.add_option("raw_data_file", input_file, "Raw data input file")->required();
    app.add_option("output_rootfile", output_file, "Output ROOT file");

    try
    {
        CLI11_PARSE(app, argc, argv);
        if (!find_events && output_file.empty())
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

    // Create output ROOT file if required
    if (!output_file.empty())
    {
        TString output_filename = output_file.c_str();
        foutput = new TFile(output_filename.Data(), "RECREATE", "PAPERO data");
        foutput->cd();

        foutput->SetCompressionLevel(compression);
        foutput->SetCompressionAlgorithm(ROOT::RCompressionSetting::EAlgorithm::kLZ4);
    }

    // Initialize TTree(s)
    std::vector<uint32_t> raw_event_buffer;
    raw_event_buffer.reserve(100000); // Pre-allocate reasonable size

    std::string alphabet = "ABCDEFGHIJKLMNOPQRSTWXYZ";
    std::vector<TTree *> raw_events_tree(max_detectors);
    std::vector<std::vector<uint32_t>> raw_event_vector(max_detectors);
    TString ttree_name;

    for (size_t detector = 0; detector < max_detectors; detector++)
    {
        TString ttree_name = (detector == 0) ? "raw_events" : TString("raw_events_") + alphabet.at(detector);
        raw_events_tree.at(detector) = new TTree(ttree_name, ttree_name);
        raw_events_tree.at(detector)->Branch("RAW Event", &raw_event_vector.at(detector));
        raw_events_tree.at(detector)->SetAutoSave(50000000); // Write every 50MB instead of default
    }

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
    uint32_t bias_voltage_0 = 0;
    uint32_t bias_voltage_1 = 0;
    uint32_t leakage_current = 0;
    
    std::streampos evt_offset;
    std::streampos last_evt_offset;
    std::streampos old_offset;
    std::streampos padding_offset;

    char dummy[100];
    float mean_rate = 0;

    bool is_new_format = false;
    std::map<uint16_t, int> detector_ids_map;

    std::vector<uint16_t> detector_ids;
    std::tuple<bool, uint32_t, uint32_t, uint8_t, uint16_t, uint16_t, std::vector<uint16_t>, uint32_t> file_retValues;
    std::tuple<bool, uint32_t, uint32_t, uint32_t, uint32_t, uint64_t, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t, int> de10_retValues;
    std::tuple<bool, timespec, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint32_t> maka_retValues;

    bool new_format = seek_file_header(file, evt_offset, verbose);

    if (new_format)
    {
        is_new_format = true;
        if (!silent)
            std::cout << "New data format" << std::endl;
        file_retValues = read_file_header(file, evt_offset, verbose);
        is_good = std::get<0>(file_retValues);
        boards = std::get<5>(file_retValues);

        // map detector_ids values to progressive number from 0 to size of detector_ids
        detector_ids = std::get<6>(file_retValues);
        for (size_t i = 0; i < detector_ids.size(); i++)
        {
            detector_ids_map[detector_ids.at(i)] = i;
        }

        old_offset = std::get<7>(file_retValues);
        evt_offset = seek_first_evt_header(file, old_offset, verbose);
        if (evt_offset != old_offset)
        {
            if (!silent)
                std::cout << "WARNING: first evt header has a " << evt_offset - old_offset << " delta value " << std::endl;
        }
        // Search for last evt header
        if (!silent)
            std::cout << "\nSearching for last evt header" << std::endl;
        last_evt_offset = seek_last_evt_header(file, verbose);

        maka_retValues = read_evt_header(file, last_evt_offset, verbose);
        if (std::get<0>(maka_retValues))
        {
            expected_events = std::get<3>(maka_retValues);
        }

        if (!silent)
            std::cout << "\tExpecting " << std::dec<< expected_events << " events in the file\n" << std::endl;

        // Go back to the first evt header
        file.seekg(evt_offset);

        if (find_events)
        {
            // Close files and exit
            if (!output_file.empty())
                foutput->Close();
            file.close();
            return 0;
        }
    }
    else
    {
        std::cerr << "ERROR: HEF data can only be of new format type, check file" << std::endl;
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
                    bias_voltage_0 = std::get<8>(de10_retValues);
                    bias_voltage_1 = std::get<9>(de10_retValues);
                    leakage_current = std::get<10>(de10_retValues);
                    evt_offset = std::get<11>(de10_retValues);

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
                        std::cout << "\tBias voltage 0: " << bias_voltage_0 << std::endl;
                        std::cout << "\tBias voltage 1: " << bias_voltage_1 << std::endl;
                        std::cout << "\tLeakage current: " << leakage_current << std::endl;
                    }

                    padding_offset = 0;
                    // HEF always uses read_eventHEF; no DAMPE / GSI variants
                    raw_event_buffer = std::move(reorder(read_eventHEF(file, evt_offset, evt_size, verbose)));

                    int det_idx = detector_ids_map.at(board_id);

                    raw_event_vector.at(det_idx) = std::move(raw_event_buffer);

                    raw_events_tree.at(det_idx)->Fill();

                    evt_offset += std::streamoff(static_cast<std::streamoff>(evt_size) * 4 + 8 + 44); // 8 is the size of the de10 footer + crc, 44 is the size of the de10 header
                }
            }
            boards_read = 0;
            evtnum++;
        }
        else
        {
            std::cout << "\nReached EOF at offset " << evt_offset << std::endl;
            break;
        }
    }

    if (!silent)
        std::cout << "\n\n\tClosing file" << std::endl;
    int filled = 0;
    std::vector<int> written_board_ids;

    for (size_t detector = 0; detector < raw_events_tree.size(); detector++)
    {
        if (raw_events_tree.at(detector)->GetEntries())
        {
            if (filled == 0)
            {
                raw_events_tree.at(detector)->SetName("raw_events");
                raw_events_tree.at(detector)->SetTitle("raw_events");
                raw_events_tree.at(detector)->Write();
            }
            else
            {
                std::string name = "raw_events_" + alphabet.substr(filled, 1);
                raw_events_tree.at(detector)->SetName(name.c_str());
                raw_events_tree.at(detector)->SetTitle(name.c_str());
                raw_events_tree.at(detector)->Write();
            }
            int real_id = (detector < detector_ids.size()) ? (int)detector_ids.at(detector) : (int)detector;
            written_board_ids.push_back(real_id);
            filled++;
        }
    }

    TTree board_id_tree("board_ids", "board_ids");
    int bid;
    board_id_tree.Branch("board_id", &bid);
    for (int id : written_board_ids)
    {
        bid = id;
        board_id_tree.Fill();
    }
    board_id_tree.Write();

    foutput->Close();
    file.close();
    return 0;
}