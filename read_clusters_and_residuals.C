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

const double stripPitch = 0.108;
const int totalStrips = 1792;

double StripToCoord(double stripPos, int board, const std::vector<int> &mirror)
{
    double centered = stripPos - (totalStrips / 2.0);
    double coord = centered * stripPitch;
    if (mirror[board])
        coord = -coord;
    return coord;
}

bool LinearFit(const std::vector<double> &x, const std::vector<double> &y, double &a, double &b)
{
    int n = static_cast<int>(x.size());
    if (n < 2)
        return false;

    double sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
    for (int i = 0; i < n; i++)
    {
        sumX += x[i];
        sumY += y[i];
        sumXY += x[i] * y[i];
        sumXX += x[i] * x[i];
    }

    double denom = n * sumXX - sumX * sumX;
    if (denom == 0)
        return false;

    b = (n * sumXY - sumX * sumY) / denom;
    a = (sumY - b * sumX) / n;

    return true;
}

int read_clusters_and_residuals(TString filename, TString output_filename, TString calibration_file)
{
    // Load the shared library
    gSystem->Load("./libcluster.so");

    const int NBoards = 8;
    const std::vector<int> SkipBoards = {0, 1, 0, 0, 0, 0, 1, 0}; // 0: include, 1: skip
    const std::vector<int> coordinate = {0, 1, 0, 1, 0, 1, 0, 1}; // 0: Y, 1: X
    const std::vector<int> mirror = {0, 0, 1, 1, 0, 0, 1, 1}; // 0: no mirror, 1: mirror
    const std::vector<int> z_pos = {0, 5, 55, 60, 110, 115, 165, 170}; // mm

    const std::vector<int> minStrip = {135, 0, 1416, 800, 135, 955, 0, 760};
    const std::vector<int> maxStrip = {383, 1791, 1660, 850, 383, 1020, 1791, 850};

    //const std::vector<int> minStrip = {0, 0, 0, 0, 0, 0, 0, 0};
    //const std::vector<int> maxStrip = {1791, 1791, 1791, 1791, 1791, 1791, 1791, 1791};

    calib cal;
    bool is_calib = read_calib(calibration_file, &cal, 1792, 0, false);

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

    std::vector<TH1F *> hResidualY(NBoards, nullptr); // boards with coordinate[b] == 0
    std::vector<TH1F *> hResidualX(NBoards, nullptr); // boards with coordinate[b] == 1

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

        if (coordinate[b] == 0)
        {
            hResidualY[b] = new TH1F(Form("hResidualY_board_%d", b),
                                      Form("YZ-track residual, board %d", b),
                                      500, -2.0, 2.0);
            hResidualY[b]->GetXaxis()->SetTitle("Residual Y (mm)");
        }
        else
        {
            hResidualX[b] = new TH1F(Form("hResidualX_board_%d", b),
                                      Form("XZ-track residual, board %d", b),
                                      500, -2.0, 2.0);
            hResidualX[b]->GetXaxis()->SetTitle("Residual X (mm)");
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

    for (Long64_t i = 0; i < nEntries; i++)
    {
        display_progress(i, nEntries);
        float cog_x = -1;
        float cog_y = -1;


        float chargeSum = 0;
        int nBoardsWithCluster = 0;

        int maxCluster_idx[NBoards] = {-1};
        float clusterPos[NBoards] = {-1};

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

            if (clusters[b]->size() == 0)
                continue;

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

            maxCluster_idx[b] = maxPos;
            
            float charge = GetClusterMIPCharge(clusters[b]->at(maxPos));
            float pos = GetClusterCOG(clusters[b]->at(maxPos));
            float eta = GetClusterEta(clusters[b]->at(maxPos));
            int seed = GetClusterSeed(clusters[b]->at(maxPos), &cal);

            clusterPos[b] = pos;

            hCharge[b]->Fill(charge);
            hAllClusterCharge->Fill(charge);
            hPos[b]->Fill(pos);
            hChargevsPos[b]->Fill(pos, charge);
            hEta[b]->Fill(eta);
            hChargevsEta[b]->Fill(eta, charge);

            chargeSum += charge;
            nBoardsWithCluster++;

        }

        // Track in YZ plane
        {
            std::vector<double> zY, coordY;
            std::vector<int> boardsY;

            for (int b = 0; b < NBoards; b++)
            {
                if (SkipBoards[b] || coordinate[b] != 0) // 0 = Y-measuring board
                    continue;
                if (maxCluster_idx[b] < 0)
                    continue;

                boardsY.push_back(b);
                zY.push_back(static_cast<double>(z_pos[b]));
                coordY.push_back(StripToCoord(clusterPos[b], b, mirror));
            }

            if (boardsY.size() >= 3)
            {
                for (size_t j = 0; j < boardsY.size(); j++)
                {
                    std::vector<double> zFit, coordFit;
                    zFit.reserve(boardsY.size() - 1);
                    coordFit.reserve(boardsY.size() - 1);
                    for (size_t k = 0; k < boardsY.size(); k++)
                    {
                        if (k == j)
                            continue;
                        zFit.push_back(zY[k]);
                        coordFit.push_back(coordY[k]);
                    }

                    double aY = 0.0, bY = 0.0;
                    if (LinearFit(zFit, coordFit, aY, bY))
                    {
                        double predicted = aY + bY * zY[j];
                        double residual = coordY[j] - predicted;
                        hResidualY[boardsY[j]]->Fill(residual);
                    }
                }
            }
        }

        // Track in XZ plane
        {
            std::vector<double> zX, coordX;
            std::vector<int> boardsX;

            for (int b = 0; b < NBoards; b++)
            {
                if (SkipBoards[b] || coordinate[b] != 1) // 1 = X-measuring board
                    continue;
                if (maxCluster_idx[b] < 0)
                    continue;

                boardsX.push_back(b);
                zX.push_back(static_cast<double>(z_pos[b]));
                coordX.push_back(StripToCoord(clusterPos[b], b, mirror));
            }

            if (boardsX.size() >= 3)
            {
                for (size_t j = 0; j < boardsX.size(); j++)
                {
                    std::vector<double> zFit, coordFit;
                    zFit.reserve(boardsX.size() - 1);
                    coordFit.reserve(boardsX.size() - 1);
                    for (size_t k = 0; k < boardsX.size(); k++)
                    {
                        if (k == j)
                            continue;
                        zFit.push_back(zX[k]);
                        coordFit.push_back(coordX[k]);
                    }

                    double aX = 0.0, bX = 0.0;
                    if (LinearFit(zFit, coordFit, aX, bX))
                    {
                        double predicted = aX + bX * zX[j];
                        double residual = coordX[j] - predicted;
                        hResidualX[boardsX[j]]->Fill(residual);
                    }
                }
            }
        }

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
        hEta[b]->Write();
        hChargevsEta[b]->Write();

        if (hResidualY[b]) hResidualY[b]->Write();
        if (hResidualX[b]) hResidualX[b]->Write();
    }
    hMeanCharge->Write();
    hAllClusterCharge->Write();

    output_file->Write();
    output_file->Close();
    f->Close();

    return 0;
}