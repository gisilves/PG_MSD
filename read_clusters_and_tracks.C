#include <TFile.h>
#include <TTree.h>
#include <TCutG.h>
#include <TSystem.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <utility>

#include "src/event.h"

#include "TH1F.h"
#include "TH2F.h"

// Compile dictionary:
// 1- rootcling -f cluster_dict.cxx -c src/event.cpp src/LinkDef.h
// 2- g++ -shared -fPIC -o libcluster.so cluster_dict.cxx $(root-config --cflags --libs)

const double stripPitch = 0.108; // mm
const int totalStrips = 1792;
const double spatialResolution = stripPitch / std::sqrt(12.0); // mm

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

double StripToCoord(double stripPos, int board, const std::vector<int> &mirror)
{
    double centered = stripPos - (totalStrips / 2.0);
    double coord = centered * stripPitch;
    if (mirror[board])
        coord = -coord;
    return coord;
}

bool LinearFit(const std::vector<double> &x, const std::vector<double> &y, double &a, double &b, double &chi2)
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

    b = (n * sumXY - sumX * sumY) / denom; // slope
    a = (sumY - b * sumX) / n;             // intercept

    chi2 = 0.0;
    double sigma2 = spatialResolution * spatialResolution;
    for (int i = 0; i < n; i++)
    {
        double fitY = a + b * x[i];
        double res = y[i] - fitY;
        chi2 += (res * res) / sigma2;
    }

    return true;
}

struct ClusterRef
{
    int board;
    size_t index;
    double strip;
    double pos;
    double z;
    double charge;
    int coordType; // 0: Y, 1: X
};

struct TrackCandidate
{
    std::vector<ClusterRef> clusters;
    double aX, bX, chi2X;
    double aY, bY, chi2Y;
    double totalChi2;
    int ndof;
};

struct ClusterPullInfo
{
    double fit_pred;
    double slope;
    double residual;
    double sigma_residual;
    double pull;
};

void generateCombinations(size_t boardIdx,
                          const std::vector<int> &activeBoards,
                          const std::vector<std::vector<ClusterRef>> &validClustersPerBoard,
                          std::vector<ClusterRef> &currentCombination,
                          std::vector<TrackCandidate> &candidates,
                          bool keepAll)
{
    if (boardIdx == activeBoards.size())
    {
        // Static buffers: the leaf does not recurse, so reuse avoids 4 allocations per combination
        static std::vector<double> zX, posX, zY, posY;
        zX.clear();
        posX.clear();
        zY.clear();
        posY.clear();
        for (const auto &cl : currentCombination)
        {
            if (cl.coordType == 1) // X
            {
                zX.push_back(cl.z);
                posX.push_back(cl.pos);
            }
            else // Y
            {
                zY.push_back(cl.z);
                posY.push_back(cl.pos);
            }
        }

        if (zX.size() < 2 || zY.size() < 2)
            return;

        TrackCandidate cand;

        if (!LinearFit(zX, posX, cand.aX, cand.bX, cand.chi2X))
            return;
        if (!LinearFit(zY, posY, cand.aY, cand.bY, cand.chi2Y))
            return;

        cand.totalChi2 = cand.chi2X + cand.chi2Y;

        // Best-only mode: skip the cluster copy unless this fit wins
        if (!keepAll && !candidates.empty() && cand.totalChi2 >= candidates[0].totalChi2)
            return;

        cand.clusters = currentCombination;
        cand.ndof = static_cast<int>(zX.size() + zY.size()) - 4;
        if (keepAll || candidates.empty())
            candidates.push_back(std::move(cand));
        else
            candidates[0] = std::move(cand);
        return;
    }

    int b = activeBoards[boardIdx];
    for (const auto &cl : validClustersPerBoard[b])
    {
        currentCombination.push_back(cl);
        generateCombinations(boardIdx + 1, activeBoards, validClustersPerBoard, currentCombination, candidates, keepAll);
        currentCombination.pop_back();
    }
}

int read_clusters_and_tracks(TString filename, TString output_filename, TString calibration_file, int events_to_process = -1, int verbose = 0)
{
    gSystem->Load("./libcluster.so");

    // Pull selection cuts
    const double targetPull = 0.0;
    const double pullTolerance = 100; // Keeps hits with | |pull| - targetPull| <= pullTolerance

    const int NBoards = 8;
    const std::vector<int> SkipBoards = {0, 1, 0, 0, 0, 0, 1, 0}; // 0: include, 1: skip
    int nNotSkippedBoards = 0;
    for (int b : SkipBoards)
    {
        if (b == 0)
            nNotSkippedBoards++;
    }

    const std::vector<int> coordinate = {0, 1, 0, 1, 0, 1, 0, 1};      // 0: Y, 1: X
    const std::vector<int> mirror = {0, 0, 1, 1, 0, 0, 1, 1};          // 0: no mirror, 1: mirror
    const std::vector<int> z_pos = {0, 5, 55, 60, 110, 115, 165, 170}; // mm

    const std::vector<int> minStrip = {0, 0, 1000, 770, 0, 960, 1000, 770};
    const std::vector<int> maxStrip = {600, 1791, 1791, 830, 800, 1020, 1791, 830};


    const std::vector<int> shift = {0, 0, 0, 0, 0, 0, 0, 0}; // strips
    calib cal;
    bool is_calib = read_calib(calibration_file, &cal, 1792, 0, false);
    if (!is_calib)
    {
        std::cerr << "Cannot read calibration file " << calibration_file << std::endl;
        return 1;
    }

    std::vector<TCutG *> regionCut(NBoards, nullptr);
    {
        double region_eta[] = {0.32, 0.36, 0.45, 0.50, 0.50, 0.47, 0.47, 0.32};
        double region_charge[] = {16.7, 17.3, 11.5, 4.4, 3.4, 3.5, 4.2, 16.6};
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

    std::vector<TH1F *> hPull(NBoards, nullptr);
    std::vector<TH2F *> hPullVsSlope(NBoards, nullptr);

    std::vector<TH1F *> hRegionPos(NBoards, nullptr);
    std::vector<TH1F *> hRegionSeed(NBoards, nullptr);
    std::vector<TH1F *> hRegionCharge(NBoards, nullptr);
    std::vector<TH1F *> hRegionEta(NBoards, nullptr);
    std::vector<TH2F *> hRegionChargeVsEta(NBoards, nullptr);
    std::vector<TH1F *> hRegionNumStrips(NBoards, nullptr);

    TH2F *hBeamProfile2D = new TH2F("hBeamProfile2D", "Beam profile 2D", 100, -0.5, 1791.5, 100, -0.5, 1791.5);
    TH1F *hMeanCharge = new TH1F("hMeanClusterCharge", "Mean cluster charge over boards", 1000, -0.5, 25.5);
    TH1F *hAllClusterCharge = new TH1F("hAllClusterCharge", "All cluster charge", 1000, -0.5, 25.5);

    auto calc_sum_sq_z = [](const std::vector<double> &z_vec)
    {
        double mean = 0.0;
        for (double z : z_vec)
            mean += z;
        mean /= z_vec.size();
        double sum_sq = 0.0;
        for (double z : z_vec)
            sum_sq += (z - mean) * (z - mean);
        return std::make_pair(mean, sum_sq);
    };

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
                              Form("Best fit cluster charge, board %d", b),
                              1000, -0.5, 25.5);
        hCharge[b]->GetXaxis()->SetTitle("Charge");

        hPos[b] = new TH1F(Form("hClusterPosition_board_%d", b),
                           Form("Best fit cluster position, board %d", b),
                           1000, -0.5, 1791.5);
        hPos[b]->GetXaxis()->SetTitle("Position");

        hChargevsPos[b] = new TH2F(Form("hClusterChargevsPos_board_%d", b),
                                   Form("Best fit cluster charge vs position, board %d", b),
                                   1000, -0.5, 1791.5, 1000, -0.5, 25.5);
        hChargevsPos[b]->GetXaxis()->SetTitle("Position");
        hChargevsPos[b]->GetYaxis()->SetTitle("Charge");

        hEta[b] = new TH1F(Form("hClusterEta_board_%d", b),
                           Form("Best fit cluster eta, board %d", b),
                           1000, 0, 1);

        hChargevsEta[b] = new TH2F(Form("hClusterChargevsEta_board_%d", b),
                                   Form("Best fit cluster charge vs eta, board %d", b),
                                   1000, 0, 1, 1000, -0.5, 25.5);
        hChargevsEta[b]->GetXaxis()->SetTitle("Eta");
        hChargevsEta[b]->GetYaxis()->SetTitle("Charge");

        hPull[b] = new TH1F(Form("hPull_board_%d", b),
                            Form("Track Pull, board %d", b),
                            1000, -20.0, 20.0);
        hPull[b]->GetXaxis()->SetTitle("Pull");
        hPull[b]->GetYaxis()->SetTitle("Entries");

        hPullVsSlope[b] = new TH2F(Form("hPullVsSlope_board_%d", b),
                                   Form("Pull vs Track Slope, board %d", b),
                                   1000, -20.0, 20.0, 1000, -0.05, 0.05);

        hPullVsSlope[b]->GetXaxis()->SetTitle("Pull");
        hPullVsSlope[b]->GetYaxis()->SetTitle("Track Slope");

        if (regionCut[b])
        {
            hRegionPos[b] = new TH1F(Form("hRegionPos_board_%d", b),
                                     Form("Position of clusters inside regionCut, board %d", b),
                                     1000, -0.5, 1791.5);
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

            hRegionChargeVsEta[b] = new TH2F(Form("hRegionChargeVsEta_board_%d", b),
                                             Form("Charge vs eta of clusters inside regionCut, board %d", b),
                                             1000, 0, 1, 1000, -0.5, 25.5);

            hRegionNumStrips[b] = new TH1F(Form("hRegionNumStrips_board_%d", b),
                                           Form("Number of strips of clusters inside regionCut, board %d", b),
                                           20, -0.5, 19.5);
            hRegionNumStrips[b]->GetXaxis()->SetTitle("Number of strips");
        }
    }

    Long64_t nEntries = -1;
    for (int b = 0; b < NBoards; b++)
    {
        if (SkipBoards[b])
            continue;
        nEntries = trees[b]->GetEntries();
        break;
    }

    if (events_to_process > 0)
        nEntries = events_to_process;

    std::vector<int> activeBoards;
    for (int b = 0; b < NBoards; b++)
    {
        if (!SkipBoards[b])
            activeBoards.push_back(b);
    }

    Long64_t nRegionSelected = 0;
    const size_t maxCombinations = 10000;
    Long64_t nSkippedCombos = 0;

    std::vector<std::vector<ClusterRef>> validClustersPerBoard(NBoards);
    std::vector<ClusterRef> currentCombination;
    std::vector<TrackCandidate> candidates;

    for (Long64_t i = 0; i < nEntries; i++)
    {
        display_progress(i, nEntries);

        for (auto &v : validClustersPerBoard)
            v.clear();
        bool allActiveHaveClusters = true;

        for (int b : activeBoards)
        {
            trees[b]->GetEntry(i);

            if (!clusters[b] || clusters[b]->empty())
            {
                allActiveHaveClusters = false;
                break;
            }

            validClustersPerBoard[b].reserve(clusters[b]->size());
            for (size_t k = 0; k < clusters[b]->size(); k++)
            {
                double cog = GetClusterCOGall(clusters[b]->at(k)) + shift[b]; // aligned strip position
                if (cog >= minStrip[b] && cog <= maxStrip[b])
                {
                    ClusterRef cl;
                    cl.board = b;
                    cl.index = k;
                    cl.strip = cog;
                    cl.pos = StripToCoord(cog, b, mirror);
                    cl.z = static_cast<double>(z_pos[b]);
                    cl.coordType = coordinate[b];
                    cl.charge = GetClusterMIPCharge(clusters[b]->at(k));
                    validClustersPerBoard[b].push_back(cl);
                }
            }

            if (validClustersPerBoard[b].empty())
            {
                allActiveHaveClusters = false;
                break;
            }
        }

        if (!allActiveHaveClusters)
            continue;

        size_t nCombos = 1;
        for (int b : activeBoards)
        {
            nCombos *= validClustersPerBoard[b].size();
            if (nCombos > maxCombinations)
                break;
        }
        if (nCombos > maxCombinations)
        {
            nSkippedCombos++;
            continue;
        }

        if (verbose)
        {
            std::cout << "========================================================\n";
            std::cout << "Event " << i << " Summary:\n";
            std::cout << "Active boards used: ";
            for (size_t idx = 0; idx < activeBoards.size(); ++idx)
            {
                std::cout << activeBoards[idx] << (idx + 1 < activeBoards.size() ? ", " : "");
            }
            std::cout << "\nValid clusters per board (within min/max strip cuts):\n";

            size_t totalCombos = 1;
            for (int b : activeBoards)
            {
                size_t nClus = validClustersPerBoard[b].size();
                totalCombos *= nClus;
                std::cout << "  Board " << b
                          << " (" << (coordinate[b] == 1 ? "X" : "Y") << ")"
                          << ": " << nClus << " cluster(s)\n";
            }
            std::cout << "Expected total combinations: " << totalCombos << "\n";
            std::cout << "--------------------------------------------------------\n";
        }

        candidates.clear();
        currentCombination.clear();
        generateCombinations(0, activeBoards, validClustersPerBoard, currentCombination, candidates, verbose >= 2);

        if (candidates.empty())
            continue;

        size_t bestIdx = 0;
        if (verbose >= 2)
        {
            std::sort(candidates.begin(), candidates.end(), [](const TrackCandidate &a, const TrackCandidate &b)
                      { return a.totalChi2 < b.totalChi2; });
        }
        else if (candidates.size() > 1)
        {
            bestIdx = static_cast<size_t>(std::distance(candidates.begin(),
                                                        std::min_element(candidates.begin(), candidates.end(), [](const TrackCandidate &a, const TrackCandidate &b)
                                                                         { return a.totalChi2 < b.totalChi2; })));
        }

        if (verbose)
        {
            std::cout << "Valid fits evaluated: " << candidates.size() << "\n";
            std::cout << "--------------------------------------------------------\n";
        }

        if (verbose >= 2)
        {
            for (size_t c = 0; c < candidates.size(); ++c)
            {
                const auto &cand = candidates[c];
                double reducedChi2 = (cand.ndof > 0) ? (cand.totalChi2 / cand.ndof) : cand.totalChi2;
                std::cout << " Rank " << c + 1 << " | Total Chi2: " << cand.totalChi2
                          << " | NDOF: " << cand.ndof
                          << " | Chi2/NDOF: " << reducedChi2 << "\n";
                std::cout << "   X-Fit -> Intercept (a): " << cand.aX << " mm, Slope (b): " << cand.bX << ", Chi2_X: " << cand.chi2X << "\n";
                std::cout << "   Y-Fit -> Intercept (a): " << cand.aY << " mm, Slope (b): " << cand.bY << ", Chi2_Y: " << cand.chi2Y << "\n";
            }
            std::cin.get();
        }
        else if (verbose == 1)
        {
            const auto &cand = candidates[bestIdx];
            double reducedChi2 = (cand.ndof > 0) ? (cand.totalChi2 / cand.ndof) : cand.totalChi2;
            std::cout << " Rank 1 | Total Chi2: " << cand.totalChi2
                      << " | NDOF: " << cand.ndof
                      << " | Chi2/NDOF: " << reducedChi2 << "\n";
            std::cout << "   X-Fit -> Intercept (a): " << cand.aX << " mm, Slope (b): " << cand.bX << ", Chi2_X: " << cand.chi2X << "\n";
            std::cout << "   Y-Fit -> Intercept (a): " << cand.aY << " mm, Slope (b): " << cand.bY << ", Chi2_Y: " << cand.chi2Y << "\n";
            std::cin.get();
        }

        const auto &cand = candidates[bestIdx];

        bool eventOk = true;
        const float maxRelDev = 0.50f;

        if (cand.clusters.empty() || nNotSkippedBoards != cand.clusters.size())
        {
            eventOk = false;
        }
        else
        {
            std::vector<float> q;
            q.reserve(cand.clusters.size());
            float meanCharge = 0.0f;
            for (const auto &cl : cand.clusters)
            {
                q.push_back(cl.charge);
                meanCharge += cl.charge;
            }
            meanCharge /= q.size();

            // Median: robust agains outliers
            std::sort(q.begin(), q.end());
            const size_t n = q.size();
            const float refCharge = (n % 2) ? q[n / 2] : 0.5f * (q[n / 2 - 1] + q[n / 2]);

            if (verbose)
                std::cout << "\t\tMean charge: " << meanCharge
                          << " | Median charge: " << refCharge << std::endl;

            int nOutliers = 0;
            for (const auto &cl : cand.clusters)
            {
                const float deltaCharge = cl.charge - refCharge;
                const bool outlier = !(refCharge > 0.0f) ||
                                     std::abs(deltaCharge) > maxRelDev * refCharge;
                if (outlier)
                    ++nOutliers;
                if (verbose)
                {
                    std::cout << " \t\tBoard " << cl.board
                              << " (" << (cl.coordType == 1 ? "X" : "Y") << ")"
                              << " | Strip: " << cl.strip
                              << " | Coord: " << cl.pos << " mm | Z: " << cl.z << " mm"
                              << " | Charge: " << cl.charge
                              << " | Delta Charge: " << deltaCharge
                              << (outlier ? " | OUTLIER" : "")
                              << std::endl;
                }
            }

            if (nOutliers > 0)
            {
                if (verbose)
                    std::cout << "\t\tWARNING: " << nOutliers
                              << " cluster(s) in best fit deviate from the median charge." << std::endl;
                eventOk = false;
            }
            else if (verbose)
            {
                std::cout << "\t\tClusters in best fit are all near each other in charge value." << std::endl;
            }
        }

        if (!eventOk)
            continue;

        // --- Calculate Pulls for the Best Fit Candidate ---
        const auto &bestFit = cand;

        std::vector<double> zX_fit, zY_fit;
        for (const auto &clRef : bestFit.clusters)
        {
            if (clRef.coordType == 1)
                zX_fit.push_back(clRef.z);
            else
                zY_fit.push_back(clRef.z);
        }

        auto [zMean_X, sumSqZ_X] = calc_sum_sq_z(zX_fit);
        auto [zMean_Y, sumSqZ_Y] = calc_sum_sq_z(zY_fit);

        double sig02 = spatialResolution * spatialResolution;

        std::vector<ClusterPullInfo> pullInfos(bestFit.clusters.size());
        bool trackSatisfiesPullCut = true;

        for (size_t idx = 0; idx < bestFit.clusters.size(); ++idx)
        {
            const auto &clRef = bestFit.clusters[idx];
            double fit_pred = 0.0;
            double slope = 0.0;
            double sigma_fit2 = 0.0;

            if (clRef.coordType == 1)
            { // X
                fit_pred = bestFit.aX + bestFit.bX * clRef.z;
                slope = bestFit.bX;
                double z_diff = clRef.z - zMean_X;
                sigma_fit2 = sig02 * ((1.0 / zX_fit.size()) + (z_diff * z_diff) / sumSqZ_X);
            }
            else
            { // Y
                fit_pred = bestFit.aY + bestFit.bY * clRef.z;
                slope = bestFit.bY;
                double z_diff = clRef.z - zMean_Y;
                sigma_fit2 = sig02 * ((1.0 / zY_fit.size()) + (z_diff * z_diff) / sumSqZ_Y);
            }

            double residual = clRef.pos - fit_pred;
            double sigma_residual = std::sqrt(std::max(1e-9, sig02 - sigma_fit2));
            double pull = residual / sigma_residual;

            pullInfos[idx] = {fit_pred, slope, residual, sigma_residual, pull};

            if (std::abs(std::abs(pull) - targetPull) > pullTolerance)
            {
                trackSatisfiesPullCut = false;
            }
        }

        if (!trackSatisfiesPullCut)
            continue;

        // --- Fill Histograms ---
        float chargeSum = 0.0;
        int nBoardsWithCluster = 0;
        float cog_x = -1.0;
        float cog_y = -1.0;

        for (size_t idx = 0; idx < bestFit.clusters.size(); ++idx)
        {
            const auto &clRef = bestFit.clusters[idx];
            const auto &pInfo = pullInfos[idx];
            int b = clRef.board;
            size_t k = clRef.index;

            const auto &rawClus = clusters[b]->at(k);

            float charge = clRef.charge;
            float pos = clRef.strip;
            float eta = GetClusterEta(rawClus, &cal);
            int seed = GetClusterSeed(rawClus, &cal);

            hPull[b]->Fill(pInfo.pull);
            hPullVsSlope[b]->Fill(pInfo.pull, pInfo.slope);

            hCharge[b]->Fill(charge);
            hAllClusterCharge->Fill(charge);
            hPos[b]->Fill(pos);
            hChargevsPos[b]->Fill(pos, charge);
            hEta[b]->Fill(eta);
            hChargevsEta[b]->Fill(eta, charge);

            if (regionCut[b] && regionCut[b]->IsInside(eta, charge))
            {
                hRegionPos[b]->Fill(pos);
                hRegionCharge[b]->Fill(charge);
                hRegionEta[b]->Fill(eta);
                hRegionChargeVsEta[b]->Fill(eta, charge);
                hRegionNumStrips[b]->Fill(rawClus.width);
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
                cog_y = pos;
            if (b == 3)
                cog_x = pos;
        }

        if (cog_x != -1.0 && cog_y != -1.0)
            hBeamProfile2D->Fill(cog_x, cog_y);

        if (nBoardsWithCluster > 0)
        {
            float meanCharge = chargeSum / nBoardsWithCluster;
            hMeanCharge->Fill(meanCharge);
        }

        if (verbose)
        {
            std::cout << "Press ENTER to continue to next event...";
            std::cin.get();
        }
    }

    std::cout << "\nClusters falling inside regionCut: " << nRegionSelected << std::endl;
    std::cout << "Events skipped (> " << maxCombinations << " combinations): " << nSkippedCombos << std::endl;

    for (int b = 0; b < NBoards; b++)
    {
        if (SkipBoards[b])
            continue;
        hCharge[b]->Write();
        hPos[b]->Write();
        hChargevsPos[b]->Write();
        hEta[b]->Write();
        hChargevsEta[b]->Write();

        hPull[b]->Write();
        hPullVsSlope[b]->Write();

        if (regionCut[b])
        {
            regionCut[b]->Write();
            hRegionPos[b]->Write();
            hRegionSeed[b]->Write();
            hRegionCharge[b]->Write();
            hRegionEta[b]->Write();
            hRegionChargeVsEta[b]->Write();
            hRegionNumStrips[b]->Write();
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