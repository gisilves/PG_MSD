#include <iostream>
#include "TFile.h"
#include "TTree.h"
#include "TString.h"

#include "miniTRB.h"

int main(int argc, char *argv[])
{

    if (argc < 3)
    {
        std::cout << "Usage: ./miniTRB_compress raw_data_file output_rootfile" << std::endl;
        return 1;
    }

    //Open binary data file
    std::fstream file(argv[1], std::ios::in | std::ios::out | std::ios::binary);
    file.seekg(0, std::ios::end);

    int version;
    int bitsize = -999;

    //Find miniTRB version
    version = seek_version(file);
    if (version == 0x1212)
    {
        std::cout << "File from 6VA miniTRB" << std::endl;
        bitsize = 1024;
    }
    else if (version == 0x1313)
    {
        std::cout << "File from 10VA miniTRB" << std::endl;
        bitsize = 2048;
    }
    else
    {
        return 1;
    }

    int fileSize = file.tellg() / bitsize; //Estimate number of events from filesize
    file.seekg(0);

    //Create output ROOT file
    TString output_filename = argv[2];
    TFile *foutput = new TFile(output_filename.Data(), "RECREATE");
    foutput->cd();

    //Initialize TTree
    TTree *raw_events = new TTree("raw_events", "raw_events");
    std::vector<unsigned short> raw_event;
    raw_events->Branch("RAW Event", &raw_event);

    bool little_endian;
    int offset;
    bool is_raw;

    //Find file endianess. TODO: implement Big Endian bytes swapping
    little_endian = seek_endianess(file);
    if (!little_endian)
    {
        std::cout << "Warning: file opened as Big Endian, event will not be read correctly!" << std::endl;
        return 1;
    }

    //Find if there is an offset before first event and the event type
    offset = seek_header(file, little_endian);
    is_raw = seek_raw(file, offset, little_endian);
    if (is_raw)
    {
        std::cout << "File is raw data" << std::endl;
    }
    else
    {
        std::cout << "Error: file is not raw data" << std::endl;
        return 1;
    }

    //Read raw events and write to TTree
    std::cout << "Trying to read " << fileSize << " events ..." << std::endl;
    int evtnum = 0;
    while (evtnum <= fileSize)
    {
        std::cout << "\rReading event " << evtnum << " of " << fileSize << std::flush;
        raw_event = read_event(file, offset, version, evtnum);
        evtnum++;
        raw_events->Fill();
    }
    raw_events->Write();
    std::cout << std::endl;
    foutput->Close();
    file.close();
    return 0;
}
