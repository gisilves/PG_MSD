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
#include "TChain.h"

struct Cluster
{
    int evt;
    int nclust;
    int seed;
    double signal;
    int width;
    int address;
    double cog;
};

typedef struct calib
{
    std::vector<float> ped;
    std::vector<float> rsig;
    std::vector<float> sig;
    std::vector<int> status;
} calib;

int read_calib(char *calib_file, calib *cal)
{

    std::ifstream in;
    in.open(calib_file);

    char comma;

    Float_t strip, va, vachannel, ped, rawsigma, sigma, status, boh;
    Int_t nlines = 0;

    std::string dummyLine;

    for (int k = 0; k < 18; k++)
    {
        getline(in, dummyLine);
    }

    while (in.good())
    {
        in >> strip >> comma >> va >> comma >> vachannel >> comma >> ped >> comma >> rawsigma >> comma >> sigma >> comma >> status >> comma >> boh;

        if (strip >= 0)
        {
            cal->ped.push_back(ped);
            cal->rsig.push_back(rawsigma);
            cal->sig.push_back(sigma);
            cal->status.push_back(status);
            nlines++;
        }
    }

    //std::cout << "Read " << nlines-1 << " lines" << std::endl;

    in.close();
    return 0;
}

int main(int argc, char *argv[])
{

    if (argc < 4)
    {
        std::cout << "Usage: ./miniTRB_clusterize <calibration file> <output_rootfile>  <first input root-filename> [second input root-filename] ..." << std::endl;
        return 1;
    }

    //Join ROOTfiles in a single chain
    TChain *chain = new TChain("raw_events");
    for (int ii = 3; ii < argc; ii++)
    {
        std::cout << "Adding file " << argv[ii] << " to the chain..." << std::endl;
        chain->Add(argv[ii]);
    }

    //Long64_t entries = chain->GetEntries();
    int entries = 1;
    printf("This run has %lld entries\n", entries);

    //Read raw event from input chain TTree
    std::vector<unsigned short> *raw_event = 0;
    TBranch *RAW = 0;
    chain->SetBranchAddress("RAW Event", &raw_event, &RAW);

    //Create output ROOTfile
    TString output_filename = argv[2];
    TFile *foutput = new TFile(output_filename.Data(), "RECREATE");
    foutput->cd();

    calib cal;
    read_calib(argv[1], &cal);

    //Loop over events
    for (int index_event = 0; index_event < entries; index_event++)
    {
        chain->GetEntry(index_event);
        std::vector<float> signal;

        if (raw_event->size() == 384 || raw_event->size() == 640)
        {
            if (cal.ped.size() >= raw_event->size())
            {
                for (std::vector<unsigned short>::size_type i = 0; i != raw_event->size(); i++)
                {
                    if (cal.status[i] == 0)
                    {
                        signal.push_back(raw_event->at(i) - cal.ped[i]);
                    }
                    else
                    {
                        signal.push_back(0);
                    }
                }
            }else
            {
                std::cout << "Error: calibration file is not compatible" << std::endl;
                return 1;
            }
            
        }
    }

    foutput->Close();
    return 0;
}
