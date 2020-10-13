#include <iostream>
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "anyoption.h"
#include <ctime>

#include "miniTRB.h"

AnyOption *opt; //Handle the option input

int main(int argc, char *argv[])
{
    bool fixVAnumber = false;
    bool logonly = false;

    opt = new AnyOption();
    opt->addUsage("Usage: ./miniTRB_compress [options] raw_data_file output_rootfile");
    opt->addUsage("");
    opt->addUsage("Options: ");
    opt->addUsage("  -h, --help       ................................. Print this help ");
    opt->addUsage("  --10to6          ................................. For DaMPE 6VA read by 10VA FOOT miniTRB");
    opt->addUsage("  --log            ................................. To log the number of events without compressing");

    opt->setFlag("help", 'h');
    opt->setFlag("10to6");
    opt->setFlag("log");

    opt->processFile("./options.txt");
    opt->processCommandArgs(argc, argv);

    char logfilename[] = "./log.txt";
    fstream logfile;
    logfile.open(logfilename, fstream::in | fstream::out | fstream::app);

    TFile *foutput;

    if (!logfile)
    {
        std::cout << "ERROR: cannot open log file" << std::endl;
        return 2;
    }

    if (!opt->hasOptions())
    { /* print usage if no options */
        opt->printUsage();
        delete opt;
        return 2;
    }

    if (opt->getValue("10to6"))
    {
        std::cout << "Reading 10VA file from 6VA detector" << std::endl;
        fixVAnumber = true;
    }

    if (opt->getValue("log"))
    {
        std::cout << "Logging only" << std::endl;
        logonly = true;
    }

    //Open binary data file
    std::fstream file(opt->getArgv(0), std::ios::in | std::ios::out | std::ios::binary);
    if (file.fail())
    {
        std::cout << "ERROR: can't open input file" << std::endl; // file could not be opened
        return 2;
    }
    int version;
    int bitsize = -999;

    std::cout << " " << std::endl;
    std::cout << "Processing file " << opt->getArgv(0) << std::endl;

    time_t unix_time = time(0);

    logfile << unix_time << " ";
    logfile << "Processing file " << opt->getArgv(0);

    //Find miniTRB version
    version = seek_version(file);
    if (version == 0x1212)
    {
        std::cout << "File from 6VA miniTRB" << std::endl;
        logfile << ", 6VA miniTRB";
        bitsize = 1024;
    }
    else if (version == 0x1313)
    {
        std::cout << "File from 10VA miniTRB" << std::endl;
        logfile << ", 10VA miniTRB";
        bitsize = 2048;
    }
    else
    {
        return 1;
    }

    file.seekg(0, std::ios::end);
    int fileSize = file.tellg() / bitsize; //Estimate number of events from filesize
    file.seekg(0);

    //Create output ROOT file
    TString output_filename = opt->getArgv(1);
    if (!logonly)
    {
        foutput = new TFile(output_filename.Data(), "RECREATE");
        foutput->cd();
    }

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
    logfile << ", read " << fileSize << " events";
    logfile << "\n";

    if (logonly)
    {
        file.close();
        logfile.close();
        return 0;
    }

    int evtnum = 0;
    while (evtnum <= fileSize)
    {
        std::cout << "\rReading event " << evtnum << " of " << fileSize << std::flush;
        raw_event = read_event(file, offset, version, evtnum);
        evtnum++;

        //Closing the "gaps" created by using the 10VA miniTRB with a 6VA sensor
        if (raw_event.size() == 640 && fixVAnumber)
        {
            raw_event.erase(raw_event.begin() + 192, raw_event.begin() + 320);
            raw_event.erase(raw_event.begin() + 384, raw_event.begin() + 512);
        }

        raw_events->Fill();
    }
    raw_events->Write();
    std::cout << std::endl;

    if (fixVAnumber)
        std::cout << "10to6 flag: remember to fix the calibration file!" << std::endl;

    foutput->Close();
    file.close();
    logfile.close();
    return 0;
}
