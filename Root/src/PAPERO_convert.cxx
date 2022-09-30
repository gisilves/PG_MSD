#include <iostream>
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TH1.h"
#include "TGraph.h"
#include "anyoption.h"
#include <ctime>
#include <tuple>

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

AnyOption *opt; // Handle the option input

int main(int argc, char *argv[])
{
    opt = new AnyOption();
    opt->addUsage("Usage: ./PAPERO_convert [options] raw_data_file output_rootfile");
    opt->addUsage("");
    opt->addUsage("Options: ");
    opt->addUsage("  -h, --help       ................................. Print this help ");
    opt->addUsage("  -v, --verbose    ................................. Verbose ");
    opt->addUsage("  --boards         ................................. Number of DE10Nano boards connected ");
    opt->addUsage("  --nevents        ................................. Number of events to be read ");
    opt->addUsage("  --gsi            ................................. To convert data from GSI hybrids (10 ADC per detector)");
    opt->setOption("boards");
    opt->setOption("nevents");

    opt->setFlag("help", 'h');
    opt->setFlag("verbose", 'v');
    opt->setFlag("gsi");

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
    foutput->SetCompressionLevel(3);
    foutput->SetCompressionAlgorithm(ROOT::kZLIB);

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
            raw_events_tree.at(detector)->Branch("RAW Event", &raw_event_vector.at(detector));
            raw_events_tree.at(detector)->SetAutoSave(0);
        }
        else
        {
            ttree_name = (TString) "raw_events_" + alphabet.at(detector);
            raw_events_tree.at(detector) = new TTree(ttree_name, ttree_name);
            ttree_name = (TString) "RAW Event " + alphabet.at(detector);
            raw_events_tree.at(detector)->Branch(ttree_name, &raw_event_vector.at(detector));
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
    bool gsi = false;
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

    if (opt->getValue("gsi"))
    {
        gsi = true;
        std::cout << "\tFormatting data for GSI hybrids" << std::endl;
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

            if (fw_version == 0xffffffff9fd68b40)
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
                raw_event_vector.at(2 * board_id).clear();
                raw_event_vector.at(2 * board_id + 1).clear();
                raw_event_vector.at(2 * board_id) = std::vector<unsigned int>(raw_event_buffer.begin(), raw_event_buffer.begin() + raw_event_buffer.size() / 2);
                raw_event_vector.at(2 * board_id + 1) = std::vector<unsigned int>(raw_event_buffer.begin() + raw_event_buffer.size() / 2, raw_event_buffer.end());
                raw_events_tree.at(2 * board_id)->Fill();
                raw_events_tree.at(2 * board_id + 1)->Fill();
            }
            else
            {
                for (int hole = 1; hole <= 10; hole++)
                {
                    raw_event_buffer.erase(raw_event_buffer.begin() + hole * 64, raw_event_buffer.begin() + (hole + 1) * 64);
                }
                raw_event_vector.at(2 * board_id).clear();
                raw_event_vector.at(2 * board_id) = raw_event_buffer;
                raw_events_tree.at(2 * board_id)->Fill();
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
