#include <iostream>
#include <fstream>
#include <vector>
#include <tuple>
#include <algorithm>
#include <ctime>

#include "CLI.hpp"
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TH1.h"
#include "TGraph.h"

#include "PAPERO.h"

#define max_detectors 16

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

int main(int argc, char *argv[])
{
    CLI::App app{"ASTRA_convert"};

    bool verbose = false;
    int boards = 0;
    int nevents = -1;
    std::string input_file;
    std::string output_file;

    app.add_flag("-v,--verbose", verbose, "Verbose output");
    app.add_option("--boards", boards, "Number of DE10 boards connected")->required();
    app.add_option("--nevents", nevents, "Number of events to read");
    app.add_option("raw_data_file", input_file, "Raw data input file")->required();
    app.add_option("output_rootfile", output_file, "Output ROOT file")->required();

    CLI11_PARSE(app, argc, argv);

    std::fstream file(input_file, std::ios::in | std::ios::out | std::ios::binary);
    if (file.fail())
    {
        std::cerr << "ERROR: can't open input file\n";
        return 2;
    }

    std::cout << "Processing file " << input_file << std::endl;

    TFile *foutput = new TFile(output_file.c_str(), "RECREATE", "PAPERO data");
    foutput->cd();

    std::vector<unsigned int> raw_event_buffer;
    std::string alphabet = "ABCDEFGHIJKLMNOPQRSTWXYZ";
    std::vector<TTree *> raw_events_tree(max_detectors);
    std::vector<std::vector<unsigned int>> raw_event_vector(max_detectors);

    for (size_t detector = 0; detector < max_detectors; detector++)
    {
        TString ttree_name = (detector == 0) ? "raw_events" : TString("raw_events_") + alphabet.at(detector);
        raw_events_tree.at(detector) = new TTree(ttree_name, ttree_name);
        std::string branch_name = (detector % 2) ? "RAW Event J7" : "RAW Event J5";
        raw_events_tree.at(detector)->Branch(branch_name.c_str(), &raw_event_vector.at(detector));
        raw_events_tree.at(detector)->SetAutoSave(0);
    }

    uint64_t offset = seek_first_evt_header(file, 0, verbose);
    int padding_offset = 0;
    bool is_good = false;
    int evtnum = 0;
    int evt_to_read = nevents;
    unsigned long fw_version = 0;
    int board_id = -1;
    int trigger_number = -1;
    int trigger_id = -1;
    int evt_size = 0;
    unsigned long timestamp = 0;
    unsigned long ext_timestamp = 0;
    int boards_read = 0;
    std::tuple<bool, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, int> evt_retValues;

    while (!file.eof())
    {
        is_good = false;
        if (evt_to_read > 0 && evtnum == evt_to_read)
            break;

        if (boards_read == 0)
            if (!read_evt_header(file, offset, verbose))
                break;

        evt_retValues = read_de10_header(file, offset, verbose);
        is_good = std::get<0>(evt_retValues);

        if (is_good)
        {
            boards_read++;
            evt_size = std::get<1>(evt_retValues);
            fw_version = std::get<2>(evt_retValues);
            trigger_number = std::get<3>(evt_retValues);
            board_id = std::get<4>(evt_retValues);
            timestamp = std::get<5>(evt_retValues);
            ext_timestamp = std::get<6>(evt_retValues);
            trigger_id = std::get<7>(evt_retValues);
            offset = std::get<8>(evt_retValues);

            std::cout << "\r\tReading event " << evtnum << std::flush;

            if (verbose)
            {
                std::cout << "\tBoard ID " << board_id << "\n";
                std::cout << "\tBoards read " << boards_read << " out of " << boards << "\n";
                std::cout << "\tTrigger ID " << trigger_id << "\n";
                std::cout << "\tFW version: " << std::hex << fw_version << std::dec << "\n";
                std::cout << "\tEvent length: " << evt_size << "\n";
                std::cout << "\tOffset: " << offset << "\n";
                std::cout << "\tTimestamp: " << timestamp << "\n";
                std::cout << "\tExt timestamp: " << ext_timestamp << "\n";
            }

            padding_offset = 0;
            raw_event_buffer.clear();
            raw_event_buffer = reorder(read_event(file, offset, evt_size, verbose, true));

            raw_event_vector.at(board_id).clear();
            raw_event_vector.at(board_id) = raw_event_buffer;
            raw_events_tree.at(board_id)->Fill();

            if (boards_read == boards)
            {
                boards_read = 0;
                evtnum++;
                offset = (int)file.tellg() + padding_offset + 8;
            }
            else
            {
                offset = (int)file.tellg() + padding_offset + 4;
                if (verbose)
                    std::cout << "WARNING: not all boards were read\n";
            }
        }
        else
        {
            break;
        }
    }

    std::cout << "\n\tClosing file after " << evtnum << " events\n";
    for (size_t detector = 0; detector < max_detectors; detector++)
        if (raw_events_tree.at(detector)->GetEntries())
            raw_events_tree.at(detector)->Write();

    foutput->Close();
    file.close();

    return 0;
}
