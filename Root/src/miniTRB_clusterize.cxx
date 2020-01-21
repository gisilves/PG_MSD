#include <iostream>
#include <fstream>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <iterator>
#include "TROOT.h"
#include "TH1.h"
#include "TFile.h"
#include "TTree.h"
#include "TChain.h"
#include "TMath.h"
#include "omp.h"

#define verbose false

typedef struct
{
    unsigned short address;
    int width;
    std::vector<float> ADC;
} cluster;

typedef struct calib
{
    std::vector<float> ped;
    std::vector<float> rsig;
    std::vector<float> sig;
    std::vector<int> status;
} calib;

int GetClusterAddress(cluster clus) { return clus.address; }
int GetClusterWidth(cluster clus) { return clus.width; }
std::vector<float> GetClusterADC(cluster clus) { return clus.ADC; }
float GetClusterSignal(cluster clus)
{
    float signal;
    std::vector<float> ADC = GetClusterADC(clus);

    for (auto &n : ADC)
    {
        signal += n;
    }
    return signal;
}

float GetClusterCOG(cluster clus)
{
    float signal;
    int address = GetClusterAddress(clus);
    std::vector<float> ADC = GetClusterADC(clus);
    float num = 0;
    float den = 0;

    for (int i = 0; i < ADC.size(); i++)
    {
        num += ADC.at(i) * (address + i);
        den += ADC.at(i);
    }
    if (den != 0)
    {
        return num / den;
    }
    else
    {
        return -999;
    }
}

int GetSeed(cluster clus)
{
    int seed;
    std::vector<float> ADC = GetClusterADC(clus);
    std::vector<float>::iterator max;

    max = std::max_element(ADC.begin(), ADC.end());

    return clus.address + std::distance(ADC.begin(), max);
}

float GetSeedADC(cluster clus)
{
    int seed;
    std::vector<float> ADC = GetClusterADC(clus);
    std::vector<float>::iterator max;

    max = std::max_element(ADC.begin(), ADC.end());

    return *max;
}

int GetVA(cluster clus)
{
    int seed = GetSeed(clus);

    return seed / 64;
}

float GetCN(std::vector<float> *signal, int va, int type)
{
    float mean = 0;
    float rms = 0;
    float cn = 0;
    int cnt = 0;

    mean = TMath::Mean(signal->begin() + (va * 64), signal->begin() + (va + 1) * 64);
    rms = TMath::RMS(signal->begin() + (va * 64), signal->begin() + (va + 1) * 64);

    // std::cout << "Mean " << mean << std::endl;
    // std::cout << "RMS " << rms << std::endl;

    if (type == 0)
    {
        for (size_t i = (va * 64); i < (va + 1) * 64; i++)
        {
            if (signal->at(i) > mean - rms && signal->at(i) < mean + rms)
            {
                cn += signal->at(i);
                cnt++;
            }
        }
        if (cnt != 0)
        {
            return cn / cnt;
        }
        else
        {
            return -999;
        }
    }
    else if (type == 1)
    {
        for (size_t i = (va * 64); i < (va + 1) * 64; i++)
        {
            if (signal->at(i) < 40)
            {
                cn += signal->at(i);
                cnt++;
            }
        }
        if (cnt != 0)
        {
            return cn / cnt;
        }
        else
        {
            return -999;
        }
    }
    else
    {
        float hard_cm = 0;
        int cnt2 = 0;
        for (size_t i = (va * 64 + 8); i < (va * 64 + 23); i++)
        {
            if (signal->at(i) < 50)
            {
                hard_cm += signal->at(i);
                cnt2++;
            }
        }
        if (cnt2 != 0)
        {
            hard_cm = hard_cm / cnt2;
        }
        else
        {
            return -999;
        }

        for (size_t i = (va * 64 + 23); i < (va * 64 + 55); i++)
        {
            if (signal->at(i) > hard_cm - 10 && signal->at(i) < hard_cm + 10)
            {
                cn += signal->at(i);
                cnt++;
            }
        }
        if (cnt != 0)
        {
            return cn / cnt;
        }
        else
        {
            return -999;
        }
    }
}

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

std::vector<cluster> clusterize(calib *cal, std::vector<float> *signal, float highThresh, float lowThresh, bool symmetric, int symmetric_width, bool absoluteThresholds = false)
{
    std::vector<cluster> clusters;
    int maxClusters = 10;
    int nclust = 0;
    cluster new_cluster;

    std::vector<int> candidate_seeds;
    std::vector<int> seeds;

    for (size_t i = 0; i < signal->size(); i++)
    {
        if (absoluteThresholds)
        {
            if (signal->at(i) > highThresh)
            {
                candidate_seeds.push_back(i);
            }
        }
        else
        {
            if (signal->at(i) / cal->sig.at(i) > highThresh)
            {
                candidate_seeds.push_back(i);
            }
        }
    }

    if (candidate_seeds.size() != 0)
    {
        seeds.push_back(candidate_seeds.at(0));

        for (size_t i = 1; i < candidate_seeds.size(); i++)
        {
            if (std::abs(candidate_seeds.at(i) - candidate_seeds.at(i - 1)) != 1)
            {
                seeds.push_back(candidate_seeds.at(i));
            }
        }

        if (seeds.size() > maxClusters || seeds.size() == 0)
        {
            throw "Error: too many seeds. ";
        }
    }
    else
    {
        //return 1;
    }

    if (seeds.size() != 0)
    {
        nclust = seeds.size();

        for (size_t seed = 0; seed < seeds.size(); seed++)
        {
            bool overThreshL, overThreshR = true;
            int L = 0;
            int R = 0;
            int width = 0;
            std::vector<float> clusterADC;

            if (symmetric)
            {
                if (seeds.at(seed) - symmetric_width > 0 && seeds.at(seed) + symmetric_width < signal->size())
                {
                    std::copy(signal->begin() + (seeds.at(seed) - symmetric_width), signal->begin() + (seeds.at(seed) + symmetric_width) + 1, back_inserter(clusterADC));

                    new_cluster.address = seeds.at(seed) - symmetric_width;
                    new_cluster.width = 2 * symmetric_width + 1;
                    new_cluster.ADC = clusterADC;
                    clusters.push_back(new_cluster);
                }
                else
                {
                    continue;
                }
            }
            else
            {
                while (overThreshL)
                {
                    if ((seeds.at(seed) - L - 1) > 0)
                    {
                        float value = 0;
                        if (absoluteThresholds)
                        {
                            value = signal->at(seeds.at(seed) - L - 1);
                        }
                        else
                        {
                            value = signal->at(seeds.at(seed) - L - 1) / cal->sig.at(seeds.at(seed) - L - 1);
                        }

                        if (value > lowThresh)
                        {
                            L++;
                        }
                        else
                        {
                            overThreshL = false;
                        }
                    }
                    else
                    {
                        overThreshL = false;
                    }
                }

                while (overThreshR)
                {
                    if ((seeds.at(seed) + R + 1) < signal->size())
                    {
                        float value = 0;
                        if (absoluteThresholds)
                        {
                            value = signal->at(seeds.at(seed) + R + 1);
                        }
                        else
                        {
                            value = signal->at(seeds.at(seed) + R + 1) / cal->sig.at(seeds.at(seed) + R + 1);
                        }

                        if (value > lowThresh)
                        {
                            R++;
                        }
                        else
                        {
                            overThreshR = false;
                        }
                    }
                    else
                    {
                        overThreshR = false;
                    }
                }
                std::copy(signal->begin() + (seeds.at(seed) - L), signal->begin() + (seeds.at(seed) + R) + 1, back_inserter(clusterADC));

                new_cluster.address = seeds.at(seed) - L;
                new_cluster.width = (R - L) + 1;
                new_cluster.ADC = clusterADC;
                clusters.push_back(new_cluster);
            }
        }
    }
    return clusters;
}

int main(int argc, char *argv[])
{

    TH1F *h1 = new TH1F("h1", "h1 title", 100, 0, 384);

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

    Long64_t entries = chain->GetEntries();
    //int entries = 1;
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

    int perc = 0;

    //Loop over events
    for (int index_event = 0; index_event < entries; index_event++)
    {
        chain->GetEntry(index_event);

        Double_t pperc = 10.0 * ((index_event + 1.0) / entries);
        if (pperc >= perc)
        {
            std::cout << "Processed " << (index_event + 1) << " out of " << entries << ":" << (int)(100.0 * (index_event + 1.0) / entries) << "%" << std::endl;
            perc++;
        }

        std::vector<float> signal;

        if (raw_event->size() == 384 || raw_event->size() == 640)
        {
            if (cal.ped.size() >= raw_event->size())
            {
                for (size_t i = 0; i != raw_event->size(); i++)
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
            }
            else
            {
                if (verbose)
                {
                    std::cout << "Error: calibration file is not compatible" << std::endl;
                }
            }
        }

        std::vector<float> signal2(signal.size());

        #pragma omp parallel for
        for (size_t i = 0; i < signal.size(); i++)
        {
            if (GetCN(&signal, i / 64, 0))
            {
                signal2.at(i) = signal.at(i) - GetCN(&signal, i / 64, 0);
            }
            else
            {
                signal2.at(i) = 0;
            }
        }

        try
        {
            std::vector<cluster> result = clusterize(&cal, &signal, 10, 2, 0, 1, false);
            for (int i = 0; i < result.size(); i++)
            {
                h1->Fill(GetClusterCOG(result.at(i)));
            }
        }
        catch (const char *msg)
        {   
            if(verbose){
            std::cerr << msg << "Skipping event " << index_event << std::endl;
            }
            continue;
        }
    }
    h1->Write();
    foutput->Close();
    return 0;
}
