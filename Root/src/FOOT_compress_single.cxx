#include <iostream>
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TH1.h"
#include "TGraph.h"
#include "anyoption.h"
#include <ctime>

#include "FOOT_single.h"

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
            reordered_vec.at(adc*128+ch) = v.at(j);
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
    int bitsize = 2712;

    std::cout << " " << std::endl;
    std::cout << "Processing file " << opt->getArgv(0) << std::endl;

    file.seekg(0, std::ios::end);
    int fileSize = file.tellg() / bitsize; //Estimate number of events from filesize
    file.seekg(0);

    //Create output ROOT file
    TString output_filename = opt->getArgv(1);

    int complevel=ROOT::CompressionSettings(ROOT::kLZMA, 2);
    foutput = new TFile(output_filename.Data(), "RECREATE", "Foot data", complevel);
    foutput->cd();

    //Initialize TTree(s)
    std::vector<unsigned int> raw_event_buffer;

    TTree *raw_events = new TTree("raw_events", "raw_events");
    std::vector<unsigned int> raw_event;
    raw_events->Branch("RAW Event", &raw_event);

    TTree *raw_events_B = new TTree("raw_events_B", "raw_events_B");
    std::vector<unsigned int> raw_event_B;
    raw_events_B->Branch("RAW Event B", &raw_event_B);

    bool little_endian = 0;
    int offset;

    //Find if there is an offset before first event and the event type
    offset = seek_header(file, little_endian);

    //std::cout << "Offset at " << offset << std::endl;

    bool master_head = chk_evt_master_header(file, little_endian, offset);

    //std::cout << "Master header is " << master_head << std::endl;

    bool builder_head = read_evt_builder_header(file, little_endian, offset);

    //std::cout << "Event builder header is " << builder_head << std::endl;

    unsigned int evt_size = read_evt_header(file, little_endian, offset);

    //std::cout << "Event size is " << std::dec << evt_size - 8 << std::endl;

    //Read raw events and write to TTree
    std::cout << "Trying to read " << fileSize << " events ..." << std::endl;

    int evtnum = 0;

    while (evtnum < fileSize)
    {
        std::cout << "\rReading event " << evtnum + 1 << " of " << fileSize << std::flush;
        raw_event_buffer = reorder(read_event(file, offset, evtnum));

        raw_event    = std::vector<unsigned int>(raw_event_buffer.begin(),raw_event_buffer.begin()+640);
        raw_event_B  = std::vector<unsigned int>(raw_event_buffer.begin()+640,raw_event_buffer.end());
        raw_events->Fill();
        raw_events_B->Fill();
        evtnum++;
    }

    raw_events->Write();
    raw_events_B->Write();
    std::cout << std::endl;

    foutput->Close();
    file.close();
    return 0;
}
