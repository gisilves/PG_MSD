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

AnyOption *opt; // Handle the option input

int main(int argc, char *argv[])
{
    opt = new AnyOption();
    opt->addUsage("Usage: ./PAPERO_info [options] raw_data_file output_rootfile");
    opt->addUsage("");
    opt->addUsage("Options: ");
    opt->addUsage("  -h, --help       ................................. Print this help ");
    opt->addUsage("  -v, --verbose    ................................. Verbose ");
    opt->addUsage("  --boards         ................................. Number of DE10Nano boards connected ");
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
    foutput = new TFile(output_filename.Data(), "RECREATE", "PAPERO info");
    foutput->cd();

    // Find if there is an offset before first event
    unsigned int offset = 0;
    offset = seek_run_header(file, offset, verbose);
    int padding_offset = 0;

    // Read raw events and boards headers info
    bool is_good = false;
    int evtnum = 0;
    int evt_to_read = -1;
    int boards = 0;
    int board_id = -1;
    int trigger_number = -1;
    int trigger_id = -1;
    int evt_size = 0;
    int boards_read = 0;
    unsigned int old_offset = 0;
    unsigned long fw_version = 0;
    uint64_t timestamp = 0;
    uint64_t first_timestamp = 0;
    long long timestamp_diff = 0;
    //uint64_t rate_timestamp[boards];
    float mean_rate = 0;
    std::tuple<bool, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, int> evt_retValues;

    if (!opt->getValue("boards"))
    {
        std::cout << "ERROR: you need to provide the number of boards connected" << std::endl;
        return 2;
    }
    else
    {
        boards = atoi(opt->getValue("boards"));
    }

    std::vector<uint64_t> rate_timestamp(boards,0);

    // Initialize TGraphs for each board
    TGraph *g_trigger_number[boards];
    TGraph *g_trigger_id[boards];
    TGraph *g_timestamp[boards];
    TGraph *g_timestamp_delta[boards - 1];
    TH1F   *h_timestamp_rate[boards];

    for (int i = 0; i < boards; i++)
    {
        g_trigger_number[i] = new TGraph();
        g_trigger_number[i]->SetName(TString::Format("g_trigger_number_board_%d", i));
        g_trigger_number[i]->SetTitle(TString::Format("Trigger number for board %d", i));
        g_trigger_number[i]->GetXaxis()->SetTitle("Event read number");
        g_trigger_number[i]->GetYaxis()->SetTitle("Trigger number");
        g_trigger_id[i] = new TGraph();
        g_trigger_id[i]->SetName(TString::Format("g_trigger_id_board_%d", i));
        g_trigger_id[i]->SetTitle(TString::Format("Trigger id for board %d", i));
        g_trigger_id[i]->GetXaxis()->SetTitle("Event read number");
        g_trigger_id[i]->GetYaxis()->SetTitle("Trigger id");
        g_timestamp[i] = new TGraph();
        g_timestamp[i]->SetName(TString::Format("g_timestamp_board_%d", i));
        g_timestamp[i]->SetTitle(TString::Format("Timestamp for board %d", i));
        g_timestamp[i]->GetXaxis()->SetTitle("Event read number");
        g_timestamp[i]->GetYaxis()->SetTitle("Timestamp");
        h_timestamp_rate[i] = new TH1F("","",10000,499900,500100);
        h_timestamp_rate[i]->SetName(TString::Format("g_timestamp_rate_board_%d", i));
        h_timestamp_rate[i]->SetTitle(TString::Format("Timestamp rate for board %d", i));
        h_timestamp_rate[i]->GetXaxis()->SetTitle("Timestamp rate");
        h_timestamp_rate[i]->GetYaxis()->SetTitle("Entries");
    }

    for (int i = 0; i < boards - 1; i++)
    {
        g_timestamp_delta[i] = new TGraph();
        g_timestamp_delta[i]->SetName(TString::Format("g_timestamp_delta_board_%d", i + 1));
        g_timestamp_delta[i]->SetTitle(TString::Format("Timestamp delta for board %d", i + 1));
        g_timestamp_delta[i]->GetXaxis()->SetTitle("Event read number");
        g_timestamp_delta[i]->GetYaxis()->SetTitle("Timestamp delta");
    }

    if (opt->getValue("nevents"))
    {
        evt_to_read = atoi(opt->getValue("nevents"));
    }

    while (!file.eof())
    {
        if (evt_to_read > 0 && evtnum == evt_to_read)
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
            trigger_id = std::get<6>(evt_retValues);
            offset = std::get<7>(evt_retValues);

            std::cout << "\r\tReading event " << evtnum << std::flush;

            g_trigger_number[boards_read - 1]->SetPoint(evtnum, evtnum, trigger_number);
            g_trigger_id[boards_read - 1]->SetPoint(evtnum, evtnum, trigger_id);
            g_timestamp[boards_read - 1]->SetPoint(evtnum, evtnum, timestamp);

            h_timestamp_rate[boards_read-1]->Fill(timestamp - rate_timestamp.at(boards_read-1));
            rate_timestamp[boards_read-1] = timestamp;

            if (boards_read == 1)
            {
                first_timestamp = timestamp;
            }
            else
            {
                timestamp_diff = first_timestamp - timestamp;
                g_timestamp_delta[boards_read - 2]->SetPoint(evtnum, evtnum, timestamp_diff);
            }

            if (verbose)
            {
                std::cout << "\tBoard ID " << board_id << std::endl;
                std::cout << "\tBoards read " << boards_read << " out of " << boards << std::endl;
                std::cout << "\tTimestamp " << std::dec << timestamp << std::endl;
                std::cout << "\tTrigger ID " << trigger_id << std::endl;
                std::cout << "\tFW version is: " << std::hex << fw_version << std::dec << std::endl;
                std::cout << "\tEvt lenght: " << evt_size << std::endl;
            }

            if (fw_version == 0xffffffff9fd68b40)
            {
                // std::cout << "\tLADDERONE!!!" << std::endl;
                padding_offset = 1024;
                board_id = board_id - 300;
            }
            else
            {
                padding_offset = 0;
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

    // Write graphs to file
    for (int i = 0; i < boards; i++)
    {
        g_trigger_number[i]->Write();
        g_trigger_id[i]->Write();
        g_timestamp[i]->Write();
        h_timestamp_rate[i]->Write();
    }

    for (int i = 0; i < boards - 1; i++)
    {
        g_timestamp_delta[i]->Write();
    }

    foutput->Close();
    file.close();
    return 0;
}
