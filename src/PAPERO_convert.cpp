#include <iostream>
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TH1.h"
#include "TGraph.h"
#include <ctime>
#include <tuple>
#include "CLI.hpp"

#include "PAPERO.h"

#define max_detectors 16

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
    constexpr int order[] = {1, 0, 3, 2, 5, 4, 7, 6, 9, 8};
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

template <typename T>
std::vector<T> reorder_DAMPE(std::vector<T> const &v)
{
    std::vector<T> reordered_vec(v.size());
    int j = 0;
    constexpr int order[] = {1, 0};
    for (int ch = 0; ch < 192; ch++)
    {
        for (int adc : order)
        {
            reordered_vec.at(adc * 192 + ch) = v.at(j);
            j++;
        }
    }
    return reordered_vec;
}

int main(int argc, char *argv[])
{
    CLI::App app{"PAPERO_convert"};

    bool verbose = false;
    bool gsi = false;
    int boards = 0;
    int nevents = -1;
    std::string input_file;
    std::string output_file;

    app.add_flag("-v,--verbose", verbose, "Verbose output");
    app.add_flag("--gsi", gsi, "To convert data from GSI hybrids (10 ADC per detector)");
    app.add_option("--boards", boards, "Number of DE10Nano boards connected (for old data format)");
    app.add_option("--nevents", nevents, "Number of events to be read");
    app.add_option("raw_data_file", input_file, "Raw data input file")->required();
    app.add_option("output_rootfile", output_file, "Output ROOT file")->required();

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

    // Create output ROOT file
    TString output_filename = output_file.c_str();
    foutput = new TFile(output_filename.Data(), "RECREATE", "PAPERO data");
    foutput->cd();
    foutput->SetCompressionLevel(3);
    foutput->SetCompressionAlgorithm(ROOT::kZLIB);

    // Initialize TTree(s)
    std::vector<uint32_t> raw_event_buffer;

    std::string alphabet = "ABCDEFGHIJKLMNOPQRSTWXYZ";
    std::vector<TTree *> raw_events_tree(max_detectors);
    std::vector<std::vector<uint32_t>> raw_event_vector(max_detectors);
    TString ttree_name;

    for (size_t detector = 0; detector < max_detectors; detector++)
    {
        if (detector == 0)
        {
            raw_events_tree.at(detector) = new TTree("raw_events", "raw_events");
            raw_events_tree.at(detector)->Branch("RAW Event J5", &raw_event_vector.at(detector));
            raw_events_tree.at(detector)->SetAutoSave(0);
        }
        else
        {
            ttree_name = (TString) "raw_events_" + alphabet.at(detector);
            raw_events_tree.at(detector) = new TTree(ttree_name, ttree_name);
            if (detector % 2)
            {
                raw_events_tree.at(detector)->Branch("RAW Event J7", &raw_event_vector.at(detector));
            }
            else
            {
                raw_events_tree.at(detector)->Branch("RAW Event J5", &raw_event_vector.at(detector));
            }
            raw_events_tree.at(detector)->SetAutoSave(0);
        }
    }

    // Find if there is an offset before file header
    bool is_good = false;
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
    std::tuple<bool, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint32_t> maka_retValues;

    bool new_format = seek_file_header(file, offset, verbose);

    if (new_format)
    {
        is_new_format = true;
        std::cout << "New data format" << std::endl;
        file_retValues = read_file_header(file, offset, verbose);
        is_good = std::get<0>(file_retValues);
        boards = std::get<5>(file_retValues);

        // map detector_ids values to progressive number from 0 to size of detector_ids
        detector_ids = std::get<6>(file_retValues);
        for (size_t i = 0; i < detector_ids.size(); i++)
        {
            detector_ids_map[detector_ids.at(i)] = i;
        }

        old_offset = std::get<7>(file_retValues);
        offset = seek_first_evt_header(file, old_offset, verbose);
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
        offset = seek_first_evt_header(file, 0, verbose);
    }

    if (gsi)
    {
        std::cout << "\tFormatting data for GSI hybrids" << std::endl;
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
        maka_retValues = read_evt_header(file, offset, verbose);
        if (std::get<0>(maka_retValues))
        {
            offset = std::get<6>(maka_retValues);
            for (size_t de10 = 0; de10 < std::get<3>(maka_retValues); de10++)
            {
                de10_retValues = read_de10_header(file, offset, verbose); // read de10 header
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


                    std::cout << "\r\tReading event " << evtnum << std::flush;

                    if (verbose == 1)
                    {
                        std::cout << "\tBoard ID " << board_id << std::endl;
                        std::cout << "\tBoards read " << boards_read << " out of " << boards << std::endl;
                        std::cout << "\tTrigger ID " << trigger_id << std::endl;
                        std::cout << "\tFW version is: " << std::hex << fw_version << std::dec << std::endl;
                        std::cout << "\tEvt lenght: " << evt_size << std::endl;
                    }

                    if (fw_version == 0x9fd68b40)
                    {
                        // std::cout << "\tLADDERONE!!!" << std::endl;
                        padding_offset = 1024;
                        board_id = board_id - 300;
                        // std::cout << "\tFixed Board ID " << board_id << std::endl;
                        raw_event_buffer.clear();
                        raw_event_buffer = reorder_DAMPE(read_event(file, offset, evt_size, verbose, false));
                    }
                    else
                    {
                        padding_offset = 0;
                        raw_event_buffer.clear();
                        raw_event_buffer = reorder(read_event(file, offset, evt_size, verbose, false));
                    }

                    if (!gsi)
                    {
                        raw_event_vector.at(2 * detector_ids_map.at(board_id)).clear();
                        raw_event_vector.at(2 * detector_ids_map.at(board_id) + 1).clear();
                        raw_event_vector.at(2 * detector_ids_map.at(board_id)) = std::vector<uint32_t>(raw_event_buffer.begin(), raw_event_buffer.begin() + raw_event_buffer.size() / 2);
                        raw_event_vector.at(2 * detector_ids_map.at(board_id) + 1) = std::vector<uint32_t>(raw_event_buffer.begin() + raw_event_buffer.size() / 2, raw_event_buffer.end());
                        raw_events_tree.at(2 * detector_ids_map.at(board_id))->Fill();
                        raw_events_tree.at(2 * detector_ids_map.at(board_id) + 1)->Fill();
                    }
                    else
                    {
                        for (int hole = 1; hole <= 10; hole++)
                        {
                            raw_event_buffer.erase(raw_event_buffer.begin() + hole * 64, raw_event_buffer.begin() + (hole + 1) * 64);
                        }
                        raw_event_vector.at(2 * detector_ids_map.at(board_id)).clear();
                        raw_event_vector.at(2 * detector_ids_map.at(board_id)) = raw_event_buffer;
                        raw_events_tree.at(2 * detector_ids_map.at(board_id))->Fill();
                    }

                    offset += evt_size * 4 + 8 + 36; // 8 is the size of the de10 footer + crc, 36 is the size of the de10 header
                }
            }
            boards_read = 0;
            evtnum++;
        }
        else
        {
            break;
        }
    }

    std::cout << "\n\tClosing file after " << std::dec << evtnum << " events" << std::endl;
    int filled = 0;

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
            filled++;
        }
    }

    foutput->Close();
    file.close();
    return 0;
}
