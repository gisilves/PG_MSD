#include <iostream>
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TH1.h"
#include "TGraph.h"
#include "anyoption.h"
#include <ctime>
#include <tuple>

#include "AMS.h"

AnyOption *opt; // Handle the option input

int main(int argc, char *argv[])
{
    opt = new AnyOption();
    opt->addUsage("Usage: ./AMS_convert [options] raw_data_file output_rootfile");
    opt->addUsage("");
    opt->addUsage("Options: ");
    opt->addUsage("  -h, --help       ................................. Print this help ");
    opt->addUsage("  -v, --verbose    ................................. Verbose ");
    opt->addUsage("  --nevents        ................................. Number of events to be read ");
    opt->setOption("nevents");

    opt->setFlag("help", 'h');
    opt->setFlag("verbose", 'v');

    opt->processFile("./options.txt");
    opt->processCommandArgs(argc, argv);

    int evt_to_read = -1;

    if (!opt->hasOptions())
    { /* print usage if no options */
        opt->printUsage();
        delete opt;
        return 2;
    }

    bool verbose = false;
    if (opt->getFlag("verbose") || opt->getFlag('v'))
        verbose = true;

    if (opt->getValue("nevents"))
    {
        evt_to_read = atoi(opt->getValue("nevents"));
    }

    TString filename = opt->getArgv(0);

    // File open
    FILE *file = fopen(filename.Data(), "rb");
    if (file == NULL)
    {
        std::cout << "Error: File " << filename.Data() << " not found" << std::endl;
        exit(2);
    }

    std::cout << " " << std::endl;
    std::cout << "Processing file " << filename.Data() << std::endl;

    std::vector<std::vector<unsigned short>> signals_by_ev;
    std::vector<std::vector<unsigned short>> signals;

    int nev = 0;
    while (!feof(file))
    {
        unsigned int read_bytes = 0;
        int ret = ProcessBlock(file, read_bytes, nev, signals_by_ev, 0);
        if (ret != 0)
            break;
        std::cout << "\r\tReading event " << nev << std::flush;
        if (nev == evt_to_read)
            break;
    }

    std::vector<unsigned short> raw_event_buffer;
    TFile *foutput = new TFile(opt->getArgv(1), "RECREATE", "Foot data");
    foutput->cd();
    foutput->SetCompressionLevel(3);
#if ROOT_VERSION_CODE >= ROOT_VERSION(6,32,0)
    foutput->SetCompressionAlgorithm(ROOT::RCompressionSetting::EAlgorithm::kZLIB);
#else
    foutput->SetCompressionAlgorithm(ROOT::kZLIB);
#endif

    TTree *raw_events_tree = new TTree("raw_events", "raw_events");
    raw_events_tree->Branch("RAW Event J5", &raw_event_buffer);
    raw_events_tree->SetAutoSave(0);
    for (auto &evt : signals_by_ev)
    {
        raw_event_buffer.clear();
        raw_event_buffer = evt;
        raw_events_tree->Fill();
    }

    raw_events_tree->Write();
    foutput->Close();

    if (file)
        fclose(file);
    file = NULL;

    std::cout << "\n\tClosing file after " << nev << " events" << std::endl;

    return 0;
}
