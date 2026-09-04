#include <TFile.h>
#include <TTree.h>
#include <vector>
#include <iostream>

#include "src/event.h"

#include "TH1F.h"
#include "TH2F.h"

// To compile dictionary:
// 1-  rootcling -f cluster_dict.cxx -c src/event.cpp src/LinkDef.h
// 2-  g++ -shared -fPIC -o libcluster.so cluster_dict.cxx  $(root-config --cflags --libs)


int read_clusters(TString filename, TString output_filename)
{
    gSystem->Load("./libcluster.so");

    const int NBoards = 8;
    const std::vector<int> SkipBoards = {0, 1, 0, 0, 0, 0, 1, 0};


    //const std::vector<int> minStrip = {135, 0, 1416, 800, 135, 955, 0, 760};
    //const std::vector<int> maxStrip = {383, 1791, 1660, 850, 383, 1020, 1791, 850};

    const std::vector<int> minStrip = {0, 0, 0, 0, 0, 0, 0, 0};
    const std::vector<int> maxStrip = {1791, 1791, 1791, 1791, 1791, 1791, 1791, 1791};

    TFile *f = TFile::Open(filename, "READ");
    if (!f || f->IsZombie())
    {
        std::cerr << "Cannot open file " << filename << std::endl;
        return 1;
    }

    TFile *output_file = new TFile(output_filename, "RECREATE");

    std::vector<TTree *> trees(NBoards, nullptr);
    std::vector<std::vector<cluster> *> clusters(NBoards, nullptr);
    std::vector<TH1F *> hCharge(NBoards, nullptr);
    std::vector<TH1F *> hPos(NBoards, nullptr);
    std::vector<TH2F *> hChargevsPos(NBoards, nullptr);

    TH2F *hBeamProfile2D = new TH2F((TString) "hBeamProfile2D", "Beam profile 2D", 100, -0.5, 1791.5, 100, -0.5, 1791.5);

    for (int b = 0; b < NBoards; b++)
    {
        if (SkipBoards[b])
            continue;

        trees[b] = (TTree *)f->Get(Form("board_%d/t_clusters_board_%d", b, b));
        if (!trees[b])
        {
            std::cerr << "Tree for board " << b << " not found!" << std::endl;
            return 1;
        }
        trees[b]->SetBranchAddress("clusters", &clusters[b]);

        hCharge[b] = new TH1F(Form("hClusterCharge_board_%d", b),
                               Form("Highest-charge cluster charge, board %d", b),
                               1000, -0.5, 25.5);
        hCharge[b]->GetXaxis()->SetTitle("Charge");

        hPos[b] = new TH1F(Form("hClusterPosition_board_%d", b),
                            Form("Highest-charge cluster position, board %d", b),
                            1000, -0.5, 1791.5);
        hPos[b]->GetXaxis()->SetTitle("Position");

        hChargevsPos[b] = new TH2F(Form("hClusterChargevsPos_board_%d", b),
                                   Form("Highest-charge cluster charge vs position, board %d", b),
                                   1000, -0.5, 1791.5, 1000, -0.5, 25.5);
        hChargevsPos[b]->GetXaxis()->SetTitle("Position");
        hChargevsPos[b]->GetYaxis()->SetTitle("Charge");
    }

    // Histogram for the mean (over boards) of the per-board highest-charge cluster
    TH1F *hMeanCharge = new TH1F("hMeanClusterCharge",
                                  "Mean cluster charge over boards", 1000, -0.5, 25.5);
    hMeanCharge->GetXaxis()->SetTitle("Mean Charge");

    // Histogram for all clusters
    TH1F *hAllClusterCharge = new TH1F("hAllClusterCharge",
                                       "All cluster charge", 1000, -0.5, 25.5);
    hAllClusterCharge->GetXaxis()->SetTitle("Charge");

    // use the first non-skipped board to define nEntries
    Long64_t nEntries = -1;
    for (int b = 0; b < NBoards; b++)
    {
        if (SkipBoards[b]) continue;
        nEntries = trees[b]->GetEntries();
        break;
    }

    for (int b = 0; b < NBoards; b++)
    {
        if (SkipBoards[b]) continue;
        if (trees[b]->GetEntries() != nEntries)
        {
            std::cerr << "Warning: board " << b << " has " << trees[b]->GetEntries()
                       << " entries, expected " << nEntries << std::endl;
        }
    }

    for (Long64_t i = 0; i < nEntries; i++)
    {
        std::cout << "\rEntry " << i << std::flush;
        float cog_x = -1;
        float cog_y = -1;

        float chargeSum = 0;
        int nBoardsWithCluster = 0;

        for (int b = 0; b < NBoards; b++)
        {
            if (SkipBoards[b])
                continue;

            trees[b]->GetEntry(i);

            if (!clusters[b] || clusters[b]->empty())
                continue;

            // find the cluster with the highest total charge on this board
            float maxSignal = -1;
            int maxPos = -1;
            for (size_t k = 0; k < clusters[b]->size(); k++)
            {
                if (GetClusterCOG(clusters[b]->at(k)) < minStrip[b] ||
                    GetClusterCOG(clusters[b]->at(k)) > maxStrip[b])
                    continue;

                float signal = GetClusterSignal(clusters[b]->at(k));
                if (signal > maxSignal)
                {
                    maxSignal = signal;
                    maxPos = k;
                }
            }

            if (maxPos < 0)
                continue;

            float charge = GetClusterMIPCharge(clusters[b]->at(maxPos));
            hCharge[b]->Fill(charge);
            hAllClusterCharge->Fill(charge);
            hPos[b]->Fill(GetClusterCOG(clusters[b]->at(maxPos)));
            hChargevsPos[b]->Fill(GetClusterCOG(clusters[b]->at(maxPos)), charge);

            chargeSum += charge;
            nBoardsWithCluster++;

            if (b == 2)
                cog_x = GetClusterCOG(clusters[b]->at(maxPos));

            if (b == 3)
                cog_y = GetClusterCOG(clusters[b]->at(maxPos));
        }

        if (cog_x != -1 && cog_y != -1)
            hBeamProfile2D->Fill(cog_x, cog_y);

        if (nBoardsWithCluster > 0)
        {
            float meanCharge = chargeSum / nBoardsWithCluster;
            hMeanCharge->Fill(meanCharge);
        }
    }
    std::cout << std::endl;

    for (int b = 0; b < NBoards; b++)
    {
        if (SkipBoards[b]) continue;
        hCharge[b]->Write();
        hPos[b]->Write();
        hChargevsPos[b]->Write();
    }
    hMeanCharge->Write();
    hAllClusterCharge->Write();
    hBeamProfile2D->Write();

    output_file->Write();
    output_file->Close();
    f->Close();

    return 0;
}