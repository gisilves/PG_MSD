#include <TFile.h>
#include <TTree.h>
#include <TSystem.h>
#include <vector>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <set>

#include <map>
#include <tuple>
#include <string>
#include <fstream>

#include "src/event.h"

#include "TF1.h"
#include "TH1F.h"
#include "TH2F.h"

// To compile dictionary:
// 1-  rootcling -f cluster_dict.cxx -c src/event.cpp src/LinkDef.h
// 2-  g++ -shared -fPIC -o libcluster.so cluster_dict.cxx  $(root-config --cflags --libs)

// ### Display progress bar ###
void display_progress(int current_event, int expected_events, int width = 50)
{
    static bool first_run = true;

    if (!first_run)
    {
        std::cout << "\033[A\r\033[2K";
        std::cout << "\033[2K";
    }
    else
    {
        first_run = false;
    }

    std::cout << "\tProcessing event " << current_event << " / " << expected_events << "\n";

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


// ### Gain table per VA ###
// CSV columns: Detector, VA, Gain
typedef std::map<std::tuple<int, int>, float> GainTable;

const int STRIPS_PER_VA = 64; // 640 strips = 10 VAs per side

int load_gain_table(const char *filename, GainTable *table)
{
    std::ifstream file(filename);
    if (!file)
        return 0;

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        
        int detector, va;
        float gain_corr;

        if (sscanf(line.c_str(), "%d %d %f", &detector, &va, &gain_corr) == 3)
            (*table)[std::make_tuple(detector, va)] = gain_corr;
    }
    return !table->empty();
}

// Returns 1.0 if the (board, side, VA) is not in the table.
double find_gain(const GainTable &table, int board, int side, double pos)
{
    int va = static_cast<int>(pos) / STRIPS_PER_VA;
    int detector = 2 * board + side;
    auto it = table.find(std::make_tuple(detector, va));
    return (it == table.end()) ? 1.0 : it->second;
}

// ### Main function ###
int compute_eta_energy_correction(TString filename, TString output_filename, TString calibration_file, TString va_gains_file = "va_gains.cal", TString eta_energy_correction_file = "eta_Energy_Calibration.cal")
{
    gSystem->Load("./libcluster.so");

    const int NBoards = 3;
    const int NSides = 2;

    const std::vector<std::vector<int>> SkipBoards = {{0, 0}, {0, 0}, {0, 0}};

    const std::vector<std::vector<int>> minStrip = {{0, 0}, {0, 0}, {0, 0}};
    const std::vector<std::vector<int>> maxStrip = {{639, 639}, {639, 639}, {639, 639}};

    const int beamBoard = 0;

    calib cal;
    bool is_calib = read_calib(calibration_file, &cal, 640, 0, false);
    if (!is_calib)
    {
        std::cerr << "Cannot read calibration file " << calibration_file << std::endl;
        return 1;
    }

    TFile *f = TFile::Open(filename, "READ");
    if (!f || f->IsZombie())
    {
        std::cerr << "Cannot open file " << filename << std::endl;
        return 1;
    }

    TFile *output_file = new TFile(output_filename, "RECREATE");

    // Load VA gain table
    GainTable gain_table;
    if (!load_gain_table(va_gains_file, &gain_table))
    {
        std::cerr << "Cannot load VA gain table" << std::endl;
        return 1;
    }
    std::cout << "Loaded gain table with " << gain_table.size() << " entries." << std::endl;

    std::vector<std::vector<TTree *>> trees(NBoards, std::vector<TTree *>(NSides, nullptr));
    std::vector<std::vector<std::vector<cluster> *>> clusters(NBoards, std::vector<std::vector<cluster> *>(NSides, nullptr));

    // Per-board histograms
    std::vector<std::vector<TH2F *>> hChargevsEta(NBoards, std::vector<TH2F *>(NSides, nullptr));
    std::vector<std::vector<TH2F *>> hChargeCorrectedvsEta(NBoards, std::vector<TH2F *>(NSides, nullptr));

    std::vector<std::vector<TDirectory *>> dirs(NBoards, std::vector<TDirectory *>(NSides, nullptr));

    for (int b = 0; b < NBoards; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s])
                continue;

            trees[b][s] = (TTree *)f->Get(Form("board_%d_side_%d/t_clusters_board_%d_side_%d", b, s, b, s));
            if (!trees[b][s])
            {
                std::cerr << "Tree for board " << b << " side " << s << " not found!" << std::endl;
                return 1;
            }
            trees[b][s]->SetBranchAddress("clusters", &clusters[b][s]);

            // Create subdirectory under output_file and cd into it BEFORE instantiating histograms
            std::string name = Form("board_%d_side_%d", b, s);
            dirs[b][s] = output_file->mkdir(name.c_str());
            dirs[b][s]->cd();

            hChargevsEta[b][s] = new TH2F(Form("hClusterChargevsEta_board_%d_side_%d", b, s),
                                          Form("Highest-charge cluster charge vs eta, board %d side %d", b, s),
                                          1000, 0, 1, 1000, -0.5, 25.5);
            hChargevsEta[b][s]->GetXaxis()->SetTitle("Eta");
            hChargevsEta[b][s]->GetYaxis()->SetTitle("Charge");


            hChargeCorrectedvsEta[b][s] = new TH2F(Form("hClusterChargeCorrectedvsEta_board_%d_side_%d", b, s),
                                                   Form("Highest-charge cluster corrected charge vs eta, board %d side %d", b, s),
                                                   1000, 0, 1, 1000, -0.5, 25.5);
            hChargeCorrectedvsEta[b][s]->GetXaxis()->SetTitle("Eta");
            hChargeCorrectedvsEta[b][s]->GetYaxis()->SetTitle("Corrected Charge");
        }
    }

    Long64_t nEntries = -1;
    for (int b = 0; b < NBoards && nEntries < 0; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s])
                continue;
            nEntries = trees[b][s]->GetEntries();
            break;
        }
    }

    for (int b = 0; b < NBoards; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s])
                continue;
            if (trees[b][s]->GetEntries() != nEntries)
            {
                std::cerr << "Warning: board " << b << " side " << s << " has "
                          << trees[b][s]->GetEntries()
                          << " entries, expected " << nEntries << std::endl;
            }
        }
    }

    for (Long64_t i = 0; i < nEntries; i++)
    {
        display_progress(i + 1, nEntries);

        for (int b = 0; b < NBoards; b++)
        {
            for (int s = 0; s < NSides; s++)
            {
                if (SkipBoards[b][s])
                    continue;

                trees[b][s]->GetEntry(i);

                if (!clusters[b][s] || clusters[b][s]->empty())
                    continue;

                // Find the cluster with the highest signal
                float maxSignal = -1;
                int maxPos = -1;
                for (size_t k = 0; k < clusters[b][s]->size(); k++)
                {
                    if (GetClusterCOG(clusters[b][s]->at(k)) < minStrip[b][s] ||
                        GetClusterCOG(clusters[b][s]->at(k)) > maxStrip[b][s])
                        continue;

                    float signal = GetClusterSignal(clusters[b][s]->at(k));
                    if (signal > maxSignal)
                    {
                        maxSignal = signal;
                        maxPos = k;
                    }
                }

                if (maxPos < 0)
                    continue;

                const cluster &best = clusters[b][s]->at(maxPos);

                float raw_charge = GetClusterMIPCharge(best);
                float pos = GetClusterCOG(best);
                int nStripsInCluster = GetClusterADC(best).size();
                float eta = GetClusterEta(best, &cal);

                // Find and apply VA gain and eta energy correction
                double gain = find_gain(gain_table, b, s, pos);
                double gain_charge = raw_charge / gain;

                // Fill raw histograms
                hChargevsEta[b][s]->Fill(eta, raw_charge);

                // Fill corrected histograms
                hChargeCorrectedvsEta[b][s]->Fill(eta, gain_charge);
            }
        }

    }
    std::cout << std::endl;

    // --- Output Writing Section ---
    for (int b = 0; b < NBoards; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s] || !dirs[b][s])
                continue;

            dirs[b][s]->cd();
            hChargevsEta[b][s]->Write();
            hChargeCorrectedvsEta[b][s]->Write();
        }
    }

    output_file->Write();
    output_file->Close();
    f->Close();

    return 0;
}