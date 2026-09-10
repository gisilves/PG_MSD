#include <TFile.h>
#include <TTree.h>
#include <TCutG.h>
#include <vector>
#include <iostream>

#include "src/event.h"

#include "TH1F.h"
#include "TH2F.h"

// To compile dictionary:
// 1-  rootcling -f cluster_dict.cxx -c src/event.cpp src/LinkDef.h
// 2-  g++ -shared -fPIC -o libcluster.so cluster_dict.cxx  $(root-config --cflags --libs)

void display_progress(int current_event, int expected_events, int width = 50)
{
    static bool first_run = true;

    if (!first_run)
    {
        std::cout << "\033[A\r\033[2K"; // Move up, go to start, clear line 1
        std::cout << "\033[2K";         // Clear line 2
    }
    else
    {
        first_run = false;
    }

    // Line 1: Status message
    std::cout << "\tProcessing event " << current_event << " / " << expected_events << "\n";

    // Line 2: Progress bar
    float progress = static_cast<float>(current_event) / static_cast<float>(expected_events);
    int pos = static_cast<int>(width * progress);

    std::cout << "\t[";
    for (int i = 0; i < width; ++i)
    {
        if (i < pos)
            std::cout << "=";
        else if (i == pos)
            std::cout << ">";
        else
            std::cout << " ";
    }
    if (current_event == expected_events)
    {
      std::cout << "] " << static_cast<int>(progress * 100.0) << "%\n\n";
    }
    else
    {
      std::cout << "] " << static_cast<int>(progress * 100.0) << "%";
    }
    std::cout.flush();
}

int read_clusters(TString filename, TString output_filename, TString calibration_file)
{
    // Load the shared library
    gSystem->Load("./libcluster.so");

    const int NBoards = 8;
    const std::vector<int> SkipBoards = {0, 1, 0, 0, 0, 0, 1, 0};


    const std::vector<int> minStrip = {135, 0, 1416, 800, 135, 955, 0, 760};
    const std::vector<int> maxStrip = {383, 1791, 1660, 850, 383, 1020, 1791, 850};

    //const std::vector<int> minStrip = {0, 0, 0, 0, 0, 0, 0, 0};
    //const std::vector<int> maxStrip = {1791, 1791, 1791, 1791, 1791, 1791, 1791, 1791};

    calib cal;
    bool is_calib = read_calib(calibration_file, &cal, 1792, 0, false);

    // -----------------------------------------------------------------
    // Region-of-interest cut in the (eta, charge) plane, used to isolate
    // the thin diagonal-line feature seen on board 0. Vertices are a
    // rough digitization of the hand-drawn region and almost certainly
    // need to be refined by overlaying regionCut[0] on the actual
    // hClusterChargevsEta_board_0 histogram and adjusting by eye
    // (e.g. in a small ROOT macro: hist->Draw("colz"); regionCut->Draw("same");).
    //
    // Only board 0 is populated for now; add entries for other boards
    // if the same feature shows up there with a different shape/position.
    // -----------------------------------------------------------------
    std::vector<TCutG *> regionCut(NBoards, nullptr);
    {
        double region_eta[]    = {0.32, 0.36, 0.45, 0.50, 0.50, 0.47, 0.47, 0.32};
        double region_charge[] = {16.7, 17.3, 11.5,  4.4,  3.4,  3.5,  4.2, 16.6};
        int nRegionPts = sizeof(region_eta) / sizeof(region_eta[0]);

        regionCut[0] = new TCutG("regionCut_board_0", nRegionPts, region_eta, region_charge);
        regionCut[0]->SetVarX("eta");
        regionCut[0]->SetVarY("charge");
        regionCut[0]->SetLineColor(kRed);
    }

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
    std::vector<TH1F *> hEta(NBoards, nullptr);
    std::vector<TH2F *> hChargevsEta(NBoards, nullptr);

    // Diagnostic histograms restricted to clusters inside regionCut[b]
    std::vector<TH1F *> hRegionPos(NBoards, nullptr);
    std::vector<TH1F *> hRegionSeed(NBoards, nullptr);
    std::vector<TH1F *> hRegionCharge(NBoards, nullptr);
    std::vector<TH1F *> hRegionEta(NBoards, nullptr);

    TH2F *hBeamProfile2D = new TH2F((TString) "hBeamProfile2D", "Beam profile 2D", 100, -0.5, 1791.5, 100, -0.5, 1791.5);

    // Small tree recording per-cluster info for every event that falls
    // inside a board's regionCut, for downstream inspection (e.g. cross-
    // checking specific strips/events in scd_viewer).
    int reg_event = -1, reg_board = -1;
    float reg_pos = -1, reg_charge = -1, reg_eta = -1;
    TTree *tRegionSel = new TTree("t_region_selected", "Clusters falling inside regionCut");
    tRegionSel->Branch("event", &reg_event, "event/I");
    tRegionSel->Branch("board", &reg_board, "board/I");
    tRegionSel->Branch("pos", &reg_pos, "pos/F");
    tRegionSel->Branch("charge", &reg_charge, "charge/F");
    tRegionSel->Branch("eta", &reg_eta, "eta/F");

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

        hEta[b] = new TH1F(Form("hClusterEta_board_%d", b),
                           Form("Highest-charge cluster eta, board %d", b),
                           1000, 0, 1);

        hChargevsEta[b] = new TH2F(Form("hClusterChargevsEta_board_%d", b),
                                   Form("Highest-charge cluster charge vs eta, board %d", b),
                                   1000, 0, 1, 1000, -0.5, 25.5);
        hChargevsEta[b]->GetXaxis()->SetTitle("Eta");
        hChargevsEta[b]->GetYaxis()->SetTitle("Charge");

        if (regionCut[b])
        {
            hRegionPos[b] = new TH1F(Form("hRegionPos_board_%d", b),
                                      Form("Position of clusters inside regionCut, board %d", b),
                                      1000, - 0.5, 1791.5);
            hRegionPos[b]->GetXaxis()->SetTitle("Position");

            hRegionSeed[b] = new TH1F(Form("hRegionSeed_board_%d", b),
                                      Form("Seed of clusters inside regionCut, board %d", b),
                                      1000, -0.5, 1791.5);
            hRegionSeed[b]->GetXaxis()->SetTitle("Seed");

            hRegionCharge[b] = new TH1F(Form("hRegionCharge_board_%d", b),
                                         Form("Charge of clusters inside regionCut, board %d", b),
                                         1000, -0.5, 25.5);
            hRegionCharge[b]->GetXaxis()->SetTitle("Charge");

            hRegionEta[b] = new TH1F(Form("hRegionEta_board_%d", b),
                                      Form("Eta of clusters inside regionCut, board %d", b),
                                      1000, 0, 1);
            hRegionEta[b]->GetXaxis()->SetTitle("Eta");
        }
    }

    // Histogram for the mean (over boards) of the per-board highest-charge cluster
    TH1F *hMeanCharge = new TH1F("hMeanClusterCharge",
                                  "Mean cluster charge over boards", 1000, -0.5, 25.5);
    hMeanCharge->GetXaxis()->SetTitle("Mean Charge (arbitrary units)");

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

    Long64_t nRegionSelected = 0;

    for (Long64_t i = 0; i < nEntries; i++)
    {
        display_progress(i, nEntries);
        float cog_x = -1;
        float cog_y = -1;

        /*
        // Check if all boards have only 1 cluster
        bool allOneCluster = true;
        for (int b = 0; b < NBoards; b++)
        {
            if (SkipBoards[b])
                continue;

            trees[b]->GetEntry(i);

            if (!clusters[b] || clusters[b]->empty())
                continue;

            if (clusters[b]->size() != 1)
            {
                allOneCluster = false;
                break;
            }
        }

        if (!allOneCluster)
            continue;
        */
       
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

            int over = clusters[b]->at(maxPos).over;

            if (over == 1)
                continue;

            float charge = GetClusterMIPCharge(clusters[b]->at(maxPos));
            float pos = GetClusterCOG(clusters[b]->at(maxPos));
            float eta = GetClusterEta(clusters[b]->at(maxPos));
            int seed = GetClusterSeed(clusters[b]->at(maxPos), &cal);

            hCharge[b]->Fill(charge);
            hAllClusterCharge->Fill(charge);
            hPos[b]->Fill(pos);
            hChargevsPos[b]->Fill(pos, charge);
            hEta[b]->Fill(eta);
            hChargevsEta[b]->Fill(eta, charge);

            // Region-of-interest selection: isolate the diagonal-line feature
            if (regionCut[b] && regionCut[b]->IsInside(eta, charge))
            {
                hRegionPos[b]->Fill(pos);
                hRegionCharge[b]->Fill(charge);
                hRegionEta[b]->Fill(eta);

                hRegionSeed[b]->Fill(seed);

                reg_event = i;
                reg_board = b;
                reg_pos = pos;
                reg_charge = charge;
                reg_eta = eta;
                tRegionSel->Fill();

                nRegionSelected++;
            }

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
    std::cout << "Clusters falling inside regionCut: " << nRegionSelected << std::endl;

    for (int b = 0; b < NBoards; b++)
    {
        if (SkipBoards[b]) continue;
        hCharge[b]->Write();
        hPos[b]->Write();
        hChargevsPos[b]->Write();
        hEta[b]->Write();
        hChargevsEta[b]->Write();

        if (regionCut[b])
        {
            regionCut[b]->Write();
            hRegionPos[b]->Write();
            hRegionSeed[b]->Write();
            hRegionCharge[b]->Write();
            hRegionEta[b]->Write();
        }
    }
    hMeanCharge->Write();
    hAllClusterCharge->Write();
    hBeamProfile2D->Write();
    tRegionSel->Write();

    output_file->Write();
    output_file->Close();
    f->Close();

    return 0;
}