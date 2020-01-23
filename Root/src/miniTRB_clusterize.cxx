#include <iostream>
#include "TFile.h"
#include "TTree.h"
#include "TChain.h"
#include "TH1.h"
#include "TH2.h"
#include "omp.h"

#include "miniTRB.h"

#define verbose false
#define NChannels 384 

int main(int argc, char *argv[])
{

    //////////////////Histos
    TH1D *hEnergyCluster = new TH1D("hEnergyCluster", "hEnergyCluster", 1000, 0, 500);
    hEnergyCluster->GetXaxis()->SetTitle("ADC");

    TH1D *hEnergyClusterSeed = new TH1D("hEnergyClusterSeeed", "hEnergyClusterSeed", 1000, 0, 500);
    hEnergyClusterSeed->GetXaxis()->SetTitle("ADC");

    TH1D *hPercentageSeed = new TH1D("hPercentageSeeed", "hPercentageSeed", 200, 20, 100);
    hPercentageSeed->GetXaxis()->SetTitle("percentage");

    TH1D *hPercSeedintegral = new TH1D("hPercSeeedintegral", "hPercSeedintegral", 200, 20, 100);
    hPercSeedintegral->GetXaxis()->SetTitle("percentage");

    TH1D *hClusterCharge = new TH1D("hClusterCharge", "hClusterCharge", 1000, -0.5, 5.5);
    hClusterCharge->GetXaxis()->SetTitle("Charge");

    TH1D *hSeedCharge = new TH1D("hSeedCharge", "hSeedCharge", 1000, -0.5, 5.5);
    hSeedCharge->GetXaxis()->SetTitle("Charge");

    TH1D *hClusterSN = new TH1D("hClusterSN", "hClusterSN", 200, 0, 100);
    hClusterSN->GetXaxis()->SetTitle("S/N");

    TH1D *hSeedSN = new TH1D("hSeedSN", "hSeedSN", 200, 0, 100);
    hSeedSN->GetXaxis()->SetTitle("S/N");

    TH1D *hClusterCog = new TH1D("hClusterCog", "hClusterCog", 150, 0, NChannels);
    hClusterCog->GetXaxis()->SetTitle("cog");

    TH1D *hSeedPos = new TH1D("hSeedPos", "hSeedPos", 150, 0, NChannels);
    hSeedPos->GetXaxis()->SetTitle("strip");

    TH1F *hNclus = new TH1F("hclus", "hclus", 10, -0.5, 9.5);
    hNclus->GetXaxis()->SetTitle("n clusters");

    TH1F *hNstrip = new TH1F("hNstrip", "hNstrip", 10, -0.5, 9.5);
    hNstrip->GetXaxis()->SetTitle("n strips");

    TH1F *hEta = new TH1F("hEta", "hEta", 100, -1, 1);
    hEta->GetXaxis()->SetTitle("Eta");

    TH2F *hADCvsWidth = new TH2F("hADCvsWidth", "hADCvsWidth", 10, -0.5, 9.5, 1000, 0, 500);
    hADCvsWidth->GetXaxis()->SetTitle("# of strips");
    hADCvsWidth->GetYaxis()->SetTitle("ADC");

    TH2F *hADCvsPos = new TH2F("hADCvsPos", "hADCvsPos", NChannels, 0, NChannels, 1000, 0, 500);
    hADCvsPos->GetXaxis()->SetTitle("cog");
    hADCvsPos->GetYaxis()->SetTitle("ADC");

    TH2F *hADCvsEta = new TH2F("hADCvsEta", "hADCvsEta", 200, -1, 1, 1000, 0, 500);
    hADCvsEta->GetXaxis()->SetTitle("eta");
    hADCvsEta->GetYaxis()->SetTitle("ADC");

    TH2F *hADCvsSN = new TH2F("hADCvsSN", "hADCvsSN", 500, 0, 50, 1000, 0, 500);
    hADCvsSN->GetXaxis()->SetTitle("S/N");
    hADCvsSN->GetYaxis()->SetTitle("ADC");

    TH2F *hNStripvsSN = new TH2F("hNstripvsSN", "hNstripvsSN", 1000, 0, 50, 5, -0.5, 4.5);
    hNStripvsSN->GetXaxis()->SetTitle("S/N");
    hNStripvsSN->GetYaxis()->SetTitle("# of strips");

    if (argc < 10)
    {
        std::cout << "Usage:\n ./miniTRB_clusterize <output_rootfile> <calibration file> <high threshold> <low threshold> <symmetric clusters> <symmetric clusters width> <use absolute thresholds> <common noise type> <first input root-filename> [second input root-filename] ..." << std::endl;
        return 1;
    }

    //Join ROOTfiles in a single chain
    TChain *chain = new TChain("raw_events");
    for (int ii = 9; ii < argc; ii++)
    {
        std::cout << "Adding file " << argv[ii] << " to the chain..." << std::endl;
        chain->Add(argv[ii]);
    }

    Long64_t entries = chain->GetEntries();
    //int entries = 2;
    printf("This run has %lld entries\n", entries);

    //Read raw event from input chain TTree
    std::vector<unsigned short> *raw_event = 0;
    TBranch *RAW = 0;
    chain->SetBranchAddress("RAW Event", &raw_event, &RAW);

    //Create output ROOTfile
    TString output_filename = argv[1];
    TFile *foutput = new TFile(output_filename.Data(), "RECREATE");
    foutput->cd();

    calib cal;
    read_calib(argv[2], &cal);

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
            if (GetCN(&signal, i / 64, atoi(argv[8])))
            {
                signal2.at(i) = signal.at(i) - GetCN(&signal, i / 64, atoi(argv[8]));
            }
            else
            {
                signal2.at(i) = 0;
            }
        }

        try
        {
            std::vector<cluster> result = clusterize(&cal, &signal2, atof(argv[3]), atof(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7]));
            for (int i = 0; i < result.size(); i++)
            {
                if (i == 0)
                {
                    hNclus->Fill(result.size());
                }
                hEnergyCluster->Fill(GetClusterSignal(result.at(i)));
                hEnergyClusterSeed->Fill(GetClusterSeedADC(result.at(i)));
                hClusterCharge->Fill(GetClusterMIPCharge(result.at(i)));
                hSeedCharge->Fill(GetSeedMIPCharge(result.at(i)));
                hPercentageSeed->Fill(100 * GetClusterSeedADC(result.at(i)) / GetClusterSignal(result.at(i)));
                hClusterSN->Fill(GetClusterSN(result.at(i), &cal));
                hSeedSN->Fill(GetSeedSN(result.at(i), &cal));
                hClusterCog->Fill(GetClusterCOG(result.at(i)));
                hSeedPos->Fill(GetClusterSeed(result.at(i)));
                hNstrip->Fill(GetClusterWidth(result.at(i)));
                hEta->Fill(GetClusterEta(result.at(i)));
                hADCvsWidth->Fill(GetClusterWidth(result.at(i)), GetClusterSignal(result.at(i)));
                hADCvsPos->Fill(GetClusterCOG(result.at(i)), GetClusterSignal(result.at(i)));
                hADCvsEta->Fill(GetClusterEta(result.at(i)), GetClusterSignal(result.at(i)));
                hADCvsSN->Fill(GetClusterSN(result.at(i), &cal), GetClusterSignal(result.at(i)));
                hNStripvsSN->Fill(GetClusterSN(result.at(i), &cal), GetClusterWidth(result.at(i)));
            }
        }
        catch (const char *msg)
        {
            if (verbose)
            {
                std::cerr << msg << "Skipping event " << index_event << std::endl;
            }
            continue;
        }
    }

    hNclus->Write();
    hEnergyCluster->Write();
    hEnergyClusterSeed->Write();
    hClusterCharge->Write();
    hSeedCharge->Write();
    hClusterSN->Write();
    hSeedSN->Write();
    hClusterCog->Write();
    hSeedPos->Write();
    hNstrip->Write();
    hEta->Write();
    hEtaRaw->Write();
    hADCvsWidth->Write();
    hADCvsPos->Write();
    hADCvsEta->Write();
    hADCvsSN->Write();
    hNStripvsSN->Write();

    Float_t sum = 0;
    for (Int_t i = 1; i <= 200; i++)
    {
        sum += hPercentageSeed->GetBinContent(i);
        hPercSeedintegral->SetBinContent(i, sum);
    }

    foutput->Close();
    return 0;
}
