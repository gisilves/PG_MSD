#include <iostream>
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TH1.h"
#include "TGraph.h"
#include "anyoption.h"
#include <ctime>
#include <tuple>

#include "FOOT.h"

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

AnyOption *opt; //Handle the option input

int main(int argc, char *argv[])
{
    opt = new AnyOption();
    opt->addUsage("Usage: ./FOOT_compress [options] raw_data_file output_rootfile");
    opt->addUsage("");
    opt->addUsage("Options: ");
    opt->addUsage("  -h, --help       ................................. Print this help ");
    opt->addUsage("  --boards         ................................. Number of DE10Nano boards connected ");
    opt->addUsage("  --gsi            ................................. To compress data from GSI hybrids (10 ADC per detector)");
    opt->setOption("boards");

    opt->setFlag("help", 'h');
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

    //Open binary data file
    std::fstream file(opt->getArgv(0), std::ios::in | std::ios::out | std::ios::binary);
    if (file.fail())
    {
        std::cout << "ERROR: can't open input file" << std::endl; // file could not be opened
        return 2;
    }
    int version;

    std::cout << " " << std::endl;
    std::cout << "Processing file " << opt->getArgv(0) << std::endl;

    //Create output ROOT file
    TString output_filename = opt->getArgv(1);

    int complevel = ROOT::CompressionSettings(ROOT::kLZMA, 2);
    foutput = new TFile(output_filename.Data(), "RECREATE", "Foot data", complevel);
    foutput->cd();

    //Initialize TTree(s)
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

    bool little_endian = 0;
    int offset = 0;

    //Find if there is an offset before first event and the event type
    offset = seek_run_header(file, little_endian);

    //Read raw events and write to TTree
    int evtnum = 0;
    int fileSize = 0;
    int boards = 0;
    bool gsi = false;
    int expected_boards = 0;
    int blank_evt_offset = 0;
    int blank_evt_num = 0;
    std::tuple<bool, int> MASTER_retValues;
    std::tuple<int, int> RCD_retValues;
    std::tuple<bool, unsigned int, int> evt_retValues;
    bool is_good = false;
    unsigned short timestamp = 0;
    unsigned short start_time = 0;
    unsigned short end_time = 0;
    int master_evt_offset = 0;
    int RCD_offset = 0;
    int evt_offset = 0;
    float mean_rate = 0;

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
    }

    while (!file.eof())
    {
        MASTER_retValues = chk_evt_master_header(file, little_endian, offset);
        master_evt_offset = std::get<1>(MASTER_retValues);

        if (std::get<0>(MASTER_retValues))
        {
            RCD_retValues = chk_evt_RCD_header(file, little_endian, master_evt_offset);

            if (!boards)
            {
                boards = std::get<0>(RCD_retValues);
            }

            RCD_offset = std::get<1>(RCD_retValues);

            if (evtnum == 0 && boards)
            {
                file.seekg(0, std::ios::end);
                std::cout << "\tTrying to read file with " << boards << " readout boards connected" << std::endl;
                if (gsi)
                {
                    std::cout << "\tFormatting data for GSI hybrids" << std::endl;
                }
                //fileSize = (int)file.tellg() / (boards * 2700 + 1); //Estimate number of events from filesize
                //std::cout << "\tEstimating " << floor(fileSize / 1000) * 1000 << " events to read ... (very unreliable estimate)" << std::endl;
                expected_boards = boards;
            }

            std::cout << "\r\tReading event " << evtnum << std::flush;

            for (int board_num = 0; board_num < boards; board_num++)
            {
                evt_retValues = read_evt_header(file, little_endian, RCD_offset, board_num);
                is_good = std::get<0>(evt_retValues);
                timestamp = std::get<1>(evt_retValues);
                evt_offset = std::get<2>(evt_retValues);

                if (evtnum == 0 && board_num == 0)
                {
                    start_time = timestamp;
                }
                else if (evtnum != 0 && board_num == boards)
                {
                    end_time = timestamp;
                }

                if (is_good)
                {
                    raw_event_buffer = reorder(read_event(file, evt_offset, board_num));

                    if (!gsi)
                    {
                        raw_event_vector.at(2 * board_num) = std::vector<unsigned int>(raw_event_buffer.begin(), raw_event_buffer.begin() + raw_event_buffer.size() / 2);
                        raw_event_vector.at(2 * board_num + 1) = std::vector<unsigned int>(raw_event_buffer.begin() + raw_event_buffer.size() / 2, raw_event_buffer.end());
                        raw_events_tree.at(2 * board_num)->Fill();
                        raw_events_tree.at(2 * board_num + 1)->Fill();
                    }
                    else
                    {
                        //std::cout << "\nFixing holes" << std::endl;

                        for (int hole = 1; hole <= 10; hole++)
                        {
                            raw_event_buffer.erase(raw_event_buffer.begin() + hole * 64, raw_event_buffer.begin() + (hole + 1) * 64);
                        }

                        raw_event_vector.at(2 * board_num) = raw_event_buffer;
                        raw_events_tree.at(2 * board_num)->Fill();
                    }
                }
                else
                    break;

                RCD_offset = evt_offset - 4;
            }

            offset = (int)file.tellg();
            evtnum++;
        }
        else
        {
            std::cout << "\n\tClosing file after " << evtnum << " events" << std::endl;
            break;
        }
    }

    mean_rate = evtnum / ((end_time - start_time) + 1);
    std::cout << "\n\nRead " << evtnum - blank_evt_num << " good events out of " << evtnum  << std::endl;

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
