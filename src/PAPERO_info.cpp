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
    opt->setOption("verbose");

    opt->setFlag("help", 'h');

    opt->processFile("./options.txt");
    opt->processCommandArgs(argc, argv);

    TFile *foutput;
    // textfile to save trigger timestamps
    std::string string_output_rootfile = opt->getArgv(1);
    std::string string_output_txtfile = string_output_rootfile.substr(0, string_output_rootfile.size() - 5) + ".txt";
    std::cout << "Output txt file: " << string_output_txtfile << std::endl;
    std::ofstream output_txt_file(string_output_txtfile);

    if (!opt->hasOptions())
    { /* print usage if no options */
        opt->printUsage();
        delete opt;
        return 2;
    }

    int verbose = 0;
    if (opt->getValue("verbose"))
    {
        verbose = atoi(opt->getValue("verbose"));
        if (verbose == 2)
        {
            //open output_txt_file
            output_txt_file.open(string_output_txtfile);
            output_txt_file << "Writing PAPERO_info (Timestamps)" << std::endl;
        }
    }

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

    unsigned int offset = 0;
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
    uint64_t i2cmsg = 0;
    uint64_t ext_timestamp = 0;
    uint64_t first_timestamp = 0;
    uint64_t first_ext_timestamp = 0;
    long long timestamp_diff = 0;
    long long ext_timestamp_diff = 0;
    float mean_rate = 0;
    float ext_mean_rate = 0;

    bool is_new_format = false;
    std::map<uint16_t, int> detector_ids_map;
    std::vector<uint16_t> detector_ids;

    std::tuple<bool, uint32_t, uint32_t, uint8_t, uint16_t, uint16_t, std::vector<uint16_t>, uint32_t> file_retValues;
    std::tuple<bool, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint64_t, uint32_t, int> de10_retValues;
    std::tuple<bool, timespec, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint32_t> maka_retValues;

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
        if (!opt->getValue("boards"))
        {
            std::cout << "ERROR: you need to provide the number of boards connected" << std::endl;
            return 2;
        }
        else
        {
            boards = atoi(opt->getValue("boards"));
        }
        offset = seek_first_evt_header(file, 0, verbose);
    }

    std::vector<uint64_t> rate_timestamp(boards, 0);
    std::vector<uint64_t> ext_rate_timestamp(boards, 0);

    // Initialize TGraphs for each board
    TGraph *g_trigger_number[boards];
    TGraph *g_trigger_id[boards];
    TGraph *g_ext_timestamp[boards];
    TGraph *g_ext_timestamp_delta[boards - 1];
    TH1F *h_ext_timestamp_rate[boards];


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
        g_ext_timestamp[i] = new TGraph();
        g_ext_timestamp[i]->SetName(TString::Format("g_ext_timestamp_board_%d", i));
        g_ext_timestamp[i]->SetTitle(TString::Format("Ext Timestamp for board %d", i));
        g_ext_timestamp[i]->GetXaxis()->SetTitle("Event read number");
        g_ext_timestamp[i]->GetYaxis()->SetTitle("Ext Timestamp");
        h_ext_timestamp_rate[i] = new TH1F("", "", 10000, 499900, 1e6);
        h_ext_timestamp_rate[i]->SetName(TString::Format("g_ext_timestamp_rate_board_%d", i));
        h_ext_timestamp_rate[i]->SetTitle(TString::Format("Ext Timestamp rate for board %d", i));
        h_ext_timestamp_rate[i]->GetXaxis()->SetTitle("Ext Timestamp rate");
        h_ext_timestamp_rate[i]->GetYaxis()->SetTitle("Entries");
    }

    for (int i = 0; i < boards - 1; i++)
    {
        g_ext_timestamp_delta[i] = new TGraph();
        g_ext_timestamp_delta[i]->SetName(TString::Format("g_ext_timestamp_delta_board_%d", i + 1));
        g_ext_timestamp_delta[i]->SetTitle(TString::Format("Ext Timestamp delta for board %d", i + 1));
        g_ext_timestamp_delta[i]->GetXaxis()->SetTitle("Event read number");
        g_ext_timestamp_delta[i]->GetYaxis()->SetTitle("Ext Timestamp delta");
    }

    if (opt->getValue("nevents"))
    {
        evt_to_read = atoi(opt->getValue("nevents"));
        std::cout << "\tReading " << evt_to_read << " events" << std::endl;
    }

    while (!file.eof())
    {
        if (evt_to_read > 0 && evtnum == evt_to_read)
            break;

        maka_retValues = read_evt_header(file, offset, verbose);

        if (verbose == 2)
        {
            timespec rawtime = std::get<1>(maka_retValues);
            //write timestamp to text file
            output_txt_file << "Event " << evtnum <<  " timestamp (s, ns) " << std::dec << rawtime.tv_sec << " " << rawtime.tv_nsec << std::endl;
        }

        if (std::get<0>(maka_retValues))
        {
            offset = std::get<7>(maka_retValues);
            for (size_t de10 = 0; de10 < std::get<4>(maka_retValues); de10++)
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

                    if(!(verbose == 2))
                    {
                    std::cout << "\r\tReading event " << evtnum << std::flush;
                    }

                    g_trigger_number[boards_read - 1]->SetPoint(evtnum, evtnum, trigger_number);
                    g_trigger_id[boards_read - 1]->SetPoint(evtnum, evtnum, trigger_id);
                    g_ext_timestamp[boards_read - 1]->SetPoint(evtnum, evtnum, ext_timestamp);

                    h_ext_timestamp_rate[boards_read - 1]->Fill((ext_timestamp - ext_rate_timestamp.at(boards_read -1)));
                    ext_rate_timestamp[boards_read - 1] = ext_timestamp;

                    if (boards_read == 1)
                    {
                        first_timestamp = i2cmsg;
                        first_ext_timestamp = ext_timestamp;
                    }
                    else
                    {
                        timestamp_diff = first_timestamp - i2cmsg;
                        ext_timestamp_diff = first_ext_timestamp - ext_timestamp;
                        g_ext_timestamp_delta[boards_read - 2]->SetPoint(evtnum, evtnum, ext_timestamp_diff);
                    }

                    if (verbose == 1)
                    {
                        std::cout << "\tBoard ID " << board_id << std::endl;
                        std::cout << "\tBoards read " << boards_read << " out of " << boards << std::endl;
                        std::cout << "\tI2C Message " << std::dec << i2cmsg << std::endl;
                        std::cout << "\tExternal Timestamp " << std::dec << ext_timestamp << std::endl;
                        std::cout << "\tTrigger ID " << trigger_id << std::endl;
                        std::cout << "\tFW version is: " << std::hex << fw_version << std::dec << std::endl;
                        std::cout << "\tEvt lenght: " << evt_size << std::endl;
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

    if(!(verbose == 2))
    {
    std::cout << "\n\tClosing file after " << std::dec << evtnum << " events" << std::endl;
    }

    // Write graphs to file
    for (int i = 0; i < boards; i++)
    {
        g_trigger_number[i]->Write();
        g_trigger_id[i]->Write();
        g_ext_timestamp[i]->Write();
        h_ext_timestamp_rate[i]->Write();
    }

    for (int i = 0; i < boards - 1; i++)
    {
        g_ext_timestamp_delta[i]->Write();
    }

    foutput->Close();
    output_txt_file.close();
    file.close();
    return 0;
}
