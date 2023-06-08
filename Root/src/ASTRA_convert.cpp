#include <iostream>
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TH1.h"
#include "TGraph.h"
#include "anyoption.h"
#include <ctime>
#include <tuple>
#include <algorithm>

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
    int nCH = 32;
    std::vector<T> reordered_vec(v.size());
    int j = 0;
    std::vector<int> order = {1, 0};
    for (int ch = 0; ch < nCH; ch++)
    {
        for (int adc = 0; adc < order.size(); adc++)
        {
            reordered_vec.at(order.at(adc) * nCH + ch) = v.at(j);
            j++;
        }
    }

    std::vector<bool> mirror = {false, true};

    for (int adc = 0; adc < order.size(); adc++)
    {
        if(mirror.at(adc))
            std::reverse(reordered_vec.begin() + (adc * nCH), reordered_vec.begin() + ((adc + 1) * nCH));
    }

    return reordered_vec;
}

AnyOption *opt; // Handle the option input

int main(int argc, char *argv[])
{
    opt = new AnyOption();
    opt->addUsage("Usage: ./ASTRA_convert [options] raw_data_file output_rootfile");
    opt->addUsage("");
    opt->addUsage("Options: ");
    opt->addUsage("  -h, --help       ................................. Print this help ");
    opt->addUsage("  -v, --verbose    ................................. Verbose ");
    opt->addUsage("  --boards         ................................. Number of DE10 boards connected ");
    opt->addUsage("  --nevents        ................................. Number of events to be read ");
    opt->setOption("boards");
    opt->setOption("nevents");

    opt->setFlag("help", 'h');
    opt->setFlag("verbose", 'v');

    opt->processFile("./options.txt");
    opt->processCommandArgs(argc, argv);

    TFile *foutput;

    if (!opt->hasOptions())
    { /* print usage if no options */
        opt->printUsage();
        delete opt;
        return 2;
    }

    bool verbose = false;
    if (opt->getFlag("verbose") || opt->getFlag('v'))
        verbose = true;

    // Open binary data file
    std::fstream file(opt->getArgv(0), std::ios::in | std::ios::out | std::ios::binary);
    if (file.fail())
    {
        std::cout << "ERROR: can't open input file" << std::endl; // file could not be opened
        return 2;
    }

    std::cout << " " << std::endl;
    std::cout << "Processing file " << opt->getArgv(0) << std::endl;

    // Create output ROOT file
    TString output_filename = opt->getArgv(1);
    foutput = new TFile(output_filename.Data(), "RECREATE", "PAPERO data");
    foutput->cd();

    // Initialize TTree(s)
    std::vector<unsigned int> raw_event_buffer;

    std::string alphabet = "ABCDEFGHIJKLMNOPQRSTWXYZ";
    std::vector<TTree *> raw_events_tree(max_detectors);
    std::vector<std::vector<unsigned int>> raw_event_vector(max_detectors);
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
            if(detector%2)
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

    // Find if there is an offset before first event
    unsigned int offset = 0;
    offset = seek_first_evt_header(file, offset, verbose);
    int padding_offset = 0;

    // Read raw events and write to TTree
    bool is_good = false;
    int evtnum = 0;
    int evt_to_read = -1;
    int boards = 0;
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

    if (!opt->getValue("boards"))
    {
        std::cout << "ERROR: you need to provide the number of boards connected" << std::endl;
        return 2;
    }
    else
    {
        boards = atoi(opt->getValue("boards"));
    }

    if (opt->getValue("nevents"))
    {
        evt_to_read = atoi(opt->getValue("nevents"));
    }

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

    for (size_t detector = 0; detector < max_detectors; detector++)
    {
        if (raw_events_tree.at(detector)->GetEntries())
        {
            raw_events_tree.at(detector)->Write();
        }
    }

    foutput->Close();
    file.close();
    return 0;
}
