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
    CLI::App app{"ASTRA_info"};

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

    // Open binary data file
    std::fstream file(input_file, std::ios::in | std::ios::out | std::ios::binary);
    if (file.fail())
    {
        std::cout << "ERROR: can't open input file" << std::endl; // file could not be opened
        return 2;
    }

    std::cout << " " << std::endl;
    std::cout << "Processing file " << input_file << std::endl;

    // Create output ROOT file
    TString output_filename = output_file.c_str();
    TFile *foutput = new TFile(output_filename, "RECREATE", "PAPERO data");
    foutput->cd();

    // Empty vector to store raw events
    std::vector<unsigned int> raw_event_buffer;

    // Timestamp TGraph
    TGraph *timestamp_graph = new TGraph();
    TGraph *timestamp_diff_graph = new TGraph();

    // Find if there is an offset before first event
    uint64_t offset = 0;
    offset = seek_first_evt_header(file, offset, verbose);
    int padding_offset = 0;

    // Read raw events and write to TTree
    bool is_good = false;
    int evtnum = 0;
    int evt_to_read = -1;
    unsigned long fw_version = 0;
    int board_id = -1;
    int trigger_number = -1;
    int trigger_id = -1;
    int evt_size = 0;
    unsigned long timestamp = 0;
    unsigned long ext_timestamp = 0;
    int boards_read = 0;
    float mean_rate = 0;
    std::tuple<bool, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, int> evt_retValues;

    unsigned int old_offset = 0;
    char dummy[100];

    if (boards == 0)
    {
        std::cout << "ERROR: you need to provide the number of boards connected" << std::endl;
        return 2;
    }

    if (nevents > 0)
    {
        evt_to_read = nevents;
    }

    int64_t previous_timestamp = 0;     // stores previous event timestamp (as signed 64-bit)
    int64_t first_timestamp = 0;        // timestamp of event 0 (kept for reference if needed)
    int64_t baseline_step = 0;          // baseline = step between event 1 and 0
    bool baseline_set = false;          // set after first observed difference

    while (!file.eof())
    {
        is_good = false;
        if (evt_to_read > 0 && evtnum == evt_to_read) // stop reading after the number of events specified
            break;

        if (boards_read == 0)
            if (!read_evt_header(file, offset, verbose)) // check for event header if this is the first board
                break;

        evt_retValues = read_de10_header(file, offset, verbose); // read de10 header
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
                std::cout << "\tBoard ID " << board_id << std::endl;
                std::cout << "\tBoards read " << boards_read << " out of " << boards << std::endl;
                std::cout << "\tTrigger ID " << trigger_id << std::endl;
                std::cout << "\tFW version is: " << std::hex << fw_version << std::dec << std::endl;
                std::cout << "\tEvt lenght: " << evt_size << std::endl;
                std::cout << "\tOffset: " << offset << std::endl;
                std::cout << "\tEvent number: " << evtnum << std::endl;
                std::cout << "\tTimestamp: " << timestamp << std::endl;
                std::cout << "\tExt timestamp: " << ext_timestamp << std::endl;
            }

            padding_offset = 0;
            raw_event_buffer.clear();
            raw_event_buffer = reorder(read_event(file, offset, evt_size, verbose, true));

            if (evtnum == 0)
            {
                first_timestamp = static_cast<int64_t>(timestamp);
                previous_timestamp = static_cast<int64_t>(timestamp);
                // Do not set baseline yet; wait for event 1
                // Add the timestamp point for event 0
                timestamp_graph->SetPoint(timestamp_graph->GetN(), evtnum, static_cast<double>(timestamp));
            }
            else
            {
                // raw difference between this event and previous event
                int64_t raw_diff = static_cast<int64_t>(timestamp) - previous_timestamp;
                // update previous timestamp to current event's timestamp
                previous_timestamp = static_cast<int64_t>(timestamp);

                // on first observed difference (event 1), set baseline
                if (!baseline_set)
                {
                    baseline_step = raw_diff;
                    baseline_set = true;
                }

                // scaled difference relative to baseline_step
                int64_t scaled_diff = raw_diff - baseline_step;

                // store points in graphs
                timestamp_graph->SetPoint(timestamp_graph->GetN(), evtnum, static_cast<double>(timestamp));
                timestamp_diff_graph->SetPoint(timestamp_diff_graph->GetN(), evtnum, static_cast<double>(scaled_diff));
            }

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
                {
                    std::cout << "WARNING: not all boards were read" << std::endl;
                }
            }
        }
        else
        {
            break;
        }
    }

    std::cout << "\n\tClosing file after " << evtnum << " events" << std::endl;
    timestamp_graph->SetName("timestamp_graph");
    timestamp_graph->Write();

    timestamp_diff_graph->SetName("timestamp_diff_graph");
    timestamp_diff_graph->Write();

    foutput->Close();
    file.close();

    // Retrive array of events with timestamp differences greater than threshold

    int threshold = 1;
    std::vector<int> evts_to_keep;
    for (int i = 0; i < timestamp_diff_graph->GetN(); i++)
    {
        if (timestamp_diff_graph->GetY()[i] > threshold)
        {
            evts_to_keep.push_back(timestamp_diff_graph->GetX()[i]);
        }
    }

    // Output csv file with EventNum and TimestampDiff (if greater than threshold)
    
    // CSV file name is the same as the output root file, but with .csv extension
    TString csv_name = output_filename;
    csv_name.ReplaceAll(".root", ".csv");
    std::ofstream output_csv_file(csv_name);
    if (output_csv_file.is_open())
    {
        for (int i = 0; i < evts_to_keep.size(); i++)
        {
            output_csv_file << evts_to_keep.at(i) << std::endl;
        }
    }
    return 0;
}
