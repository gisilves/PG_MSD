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
    bool fixVAnumber = false;

    opt = new AnyOption();
    opt->addUsage("Usage: ./FOOT_compress [options] raw_data_file output_rootfile");
    opt->addUsage("");
    opt->addUsage("Options: ");
    opt->addUsage("  -h, --help       ................................. Print this help ");

    opt->setFlag("help", 'h');

    opt->processFile("./options.txt");
    opt->processCommandArgs(argc, argv);

    TFile *foutput;

    TGraph *header = new TGraph();
    header->SetName("header");
    header->SetMarkerSize(0.5);
    unsigned int goodevents = 0;

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

    TTree *raw_events = new TTree("raw_events", "raw_events");
    std::vector<unsigned int> raw_event;
    raw_events->Branch("RAW Event", &raw_event);
    raw_events->SetAutoSave(0);
    TTree *raw_events_B = new TTree("raw_events_B", "raw_events_B");
    std::vector<unsigned int> raw_event_B;
    raw_events_B->Branch("RAW Event B", &raw_event_B);
    raw_events_B->SetAutoSave(0);

    TTree *raw_events_C = new TTree("raw_events_C", "raw_events_C");
    std::vector<unsigned int> raw_event_C;
    raw_events_C->Branch("RAW Event C", &raw_event_C);
    raw_events_C->SetAutoSave(0);
    TTree *raw_events_D = new TTree("raw_events_D", "raw_events_D");
    std::vector<unsigned int> raw_event_D;
    raw_events_D->Branch("RAW Event D", &raw_event_D);
    raw_events_D->SetAutoSave(0);

    TTree *raw_events_E = new TTree("raw_events_E", "raw_events_E");
    std::vector<unsigned int> raw_event_E;
    raw_events_E->Branch("RAW Event E", &raw_event_E);
    raw_events_E->SetAutoSave(0);
    TTree *raw_events_F = new TTree("raw_events_F", "raw_events_F");
    std::vector<unsigned int> raw_event_F;
    raw_events_F->Branch("RAW Event F", &raw_event_F);
    raw_events_F->SetAutoSave(0);

    bool little_endian = 0;
    int offset = 0;

    //Find if there is an offset before first event and the event type
    offset = seek_run_header(file, little_endian);

    //Read raw events and write to TTree
    int evtnum = 0;
    int fileSize = 0;
    int boards = 0;
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

    char dummy[200];

    while (!file.eof())
    {
        MASTER_retValues = chk_evt_master_header(file, little_endian, offset);
        master_evt_offset = std::get<1>(MASTER_retValues);

        if (std::get<0>(MASTER_retValues))
        {
            RCD_retValues = chk_evt_RCD_header(file, little_endian, master_evt_offset);
            boards = std::get<0>(RCD_retValues);
            RCD_offset = std::get<1>(RCD_retValues);

            if (evtnum == 0 && boards)
            {
                file.seekg(0, std::ios::end);
                std::cout << "\tTrying to read file with " << boards << " readout boards connected" << std::endl;
                fileSize = (int)file.tellg() / (boards * 2700 + 1); //Estimate number of events from filesize
                std::cout << "\tEstimating " << floor(fileSize / 1000) * 1000 << " events to read ... (very unreliable estimate)" << std::endl;
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
                else if (evtnum != 0 && board_num == 0)
                {
                    end_time = timestamp;
                }

                if (is_good)
                {
                    raw_event_buffer = reorder(read_event(file, evt_offset, board_num));

                    if (board_num == 0)
                    {
                        //std::cout << "\nBoard number " << board_num << std::endl;
                        raw_event = std::vector<unsigned int>(raw_event_buffer.begin(), raw_event_buffer.begin() + 640);
                        raw_event_B = std::vector<unsigned int>(raw_event_buffer.begin() + 640, raw_event_buffer.end());
                        raw_events->Fill();
                        raw_events_B->Fill();
                    }
                    else if (board_num == 1)
                    {
                        //std::cout << "\nBoard number " << board_num << std::endl;
                        raw_event_C = std::vector<unsigned int>(raw_event_buffer.begin(), raw_event_buffer.begin() + 640);
                        raw_event_D = std::vector<unsigned int>(raw_event_buffer.begin() + 640, raw_event_buffer.end());
                        raw_events_C->Fill();
                        raw_events_D->Fill();
                    }
                    else if (board_num == 2)
                    {
                        //std::cout << "\nBoard number " << board_num << std::endl;
                        raw_event_E = std::vector<unsigned int>(raw_event_buffer.begin(), raw_event_buffer.begin() + 640);
                        raw_event_F = std::vector<unsigned int>(raw_event_buffer.begin() + 640, raw_event_buffer.end());
                        raw_events_E->Fill();
                        raw_events_F->Fill();
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
    std::cout << "\n\nRead " << evtnum - blank_evt_num << " good events out of " << evtnum << " acquired with a mean rate of " << mean_rate << " Hz" << std::endl;

    raw_events->Write();
    raw_events_B->Write();

    if (raw_events_C->GetEntries() && raw_events_D->GetEntries())
    {
        raw_events_C->Write();
        raw_events_D->Write();
    }

    if (raw_events_E->GetEntries() && raw_events_F->GetEntries())
    {
        raw_events_E->Write();
        raw_events_F->Write();
    }

    foutput->Close();
    file.close();
    return 0;
}
