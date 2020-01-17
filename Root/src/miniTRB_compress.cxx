#include <iostream>
#include <fstream>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <iterator>
#include "TROOT.h"
#include "TFile.h"
#include "TTree.h"
#include "TString.h"

int seek_endianess(std::fstream &file)
{
    bool found = false;
    bool little_endianess;
    unsigned char buffer[2];
    unsigned short val;

    file.seekg(0);

    while (!found && !file.eof())
    {
        file.read(reinterpret_cast<char *>(&buffer), 2);
        val = buffer[0] | (buffer[1] << 8);

        if (val == 0xbbaa)
        {
            little_endianess = false;
            found = true;
        }
        else if (val == 0xaabb)
        {
            little_endianess = true;
            found = true;
        }
    }

    if (!found)
    {
        std::cout << "Can't find endianess WORD in the file" << std::endl;
        return -999;
    }
    else
    {
        return little_endianess;
    }
}

int seek_header(std::fstream &file, bool little_endian)
{
    int offset = 0;
    unsigned short header;
    bool found = false;

    unsigned char buffer[2];
    unsigned short val;

    if (little_endian)
    {
        header = 0xeb90;
    }
    else
    {
        header = 0x90eb;
    }

    while (!found && !file.eof())
    {
        file.seekg(offset);
        file.read(reinterpret_cast<char *>(&buffer), 2);
        val = buffer[0] | (buffer[1] << 8);

        if (val == header)
        {
            found = true;
        }
        else
        {
            offset += 2;
        }
    }

    if (!found)
    {
        std::cout << "Can't find event header in the file" << std::endl;
        return -999;
    }
    else
    {
        return offset;
    }
}

int seek_raw(std::fstream &file, int offset, bool little_endian)
{
    bool found = false;
    bool is_raw = 0;
    unsigned char buffer[2];
    unsigned short val;

    file.seekg(offset + 2);
    file.read(reinterpret_cast<char *>(&buffer), 2);

    if (little_endian)
    {
        val = (int)buffer[1];
    }
    else
    {
        val = (int)buffer[0];
    }

    if (val == 0xa0)
    {
        is_raw = true;
        found = true;
    }

    return is_raw;
}

int seek_version(std::fstream &file)
{
    bool found = false;
    int version;
    unsigned char buffer[2];
    unsigned short val;

    file.seekg(0);

    while (!found && !file.eof())
    {
        file.read(reinterpret_cast<char *>(&buffer), 2);
        val = buffer[0] | (buffer[1] << 8);

        if (val == 0x1212 || val == 0x1313)
        {
            version = val;
            found = true;
        }
    }

    if (!found)
    {
        std::cout << "Can't find version WORD in the file" << std::endl;
        return -999;
    }
    else
    {
        return version;
    }
}

std::vector<unsigned short> read_event(std::fstream &file, int offset, int version, int evt)
{
    file.seekg(offset + 4 + evt * 1024);

    int event_size;

    if (version == 0x1212)
    {
        event_size = 384;
    }
    else if (version == 0x1313)
    {
        event_size = 640;
    }
    else
    {
        std::cout << "Error: unknown miniTRB version" << std::endl;
    }

    std::vector<unsigned short> buffer(event_size);
    file.read(reinterpret_cast<char *>(buffer.data()), buffer.size() * 2);
    std::streamsize s = file.gcount();
    buffer.resize(s / 2);

    std::vector<unsigned short> event(s / 2);

    int j = 0;
    for (int i = 0; i < buffer.size(); i += 2)
    {
        event.at(j) = buffer.at(i);
        event.at(j + buffer.size() / 2) = buffer.at(i + 1);
        j++;
    }

    return event;
}

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
    int fileSize = file.tellg() / 1024; //Estimate number of events from filesize
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
    int version;

    //Find miniTRB version
    version = seek_version(file);
    if (version == 0x1212)
    {
        std::cout << "File from 6VA miniTRB" << std::endl;
    }
    else if (version == 0x1313)
    {
        std::cout << "File from 10VA miniTRB" << std::endl;
    }

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
