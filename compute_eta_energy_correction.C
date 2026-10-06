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
#include <iomanip>

#include "src/event.h"

#include "TF1.h"
#include "TH1F.h"
#include "TH2F.h"
#include "TGraphErrors.h"
#include "TCanvas.h"

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

// ### Helper: Histogram Creation ###
// Creates one TDirectory per (board, side) in output_file and books the histograms inside it.
void create_histograms(
    int NBoards,
    int NSides,
    const std::vector<std::vector<int>> &SkipBoards,
    TFile *output_file,
    std::vector<std::vector<TDirectory *>> &dirs,
    std::vector<std::vector<TH2F *>> &hChargevsEta,
    std::vector<std::vector<TH2F *>> &hChargeCorrectedvsEta)
{
    for (int b = 0; b < NBoards; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s])
                continue;

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
}

// ### Helper: Histogram Filling ###
void fill_histograms(
    Long64_t nEntries,
    int NBoards,
    int NSides,
    const std::vector<std::vector<int>> &SkipBoards,
    const std::vector<std::vector<int>> &minStrip,
    const std::vector<std::vector<int>> &maxStrip,
    std::vector<std::vector<TTree *>> &trees,
    std::vector<std::vector<std::vector<cluster> *>> &clusters,
    calib &cal,
    const GainTable &gain_table,
    std::vector<std::vector<TH2F *>> &hChargevsEta,
    std::vector<std::vector<TH2F *>> &hChargeCorrectedvsEta)
{
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
                float eta = GetClusterEta(best, &cal);

                // Find and apply VA gain
                double gain = find_gain(gain_table, b, s, pos);
                double gain_charge = raw_charge / gain;

                hChargevsEta[b][s]->Fill(eta, raw_charge);
                hChargeCorrectedvsEta[b][s]->Fill(eta, gain_charge);
            }
        }
    }
    std::cout << std::endl;
}

// ### Helper: Rebinned Histogram Creation ###
// Clones hChargeCorrectedvsEta into its board/side subdirectory and rebins the eta axis.
void create_rebinned_histograms(
    int NBoards,
    int NSides,
    const std::vector<std::vector<int>> &SkipBoards,
    std::vector<std::vector<TDirectory *>> &dirs,
    std::vector<std::vector<TH2F *>> &hChargeCorrectedvsEta,
    std::vector<std::vector<TH2F *>> &hChargeCorrectedvsEta2,
    int rebinFactor)
{
    for (int b = 0; b < NBoards; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s] || !dirs[b][s])
                continue;

            dirs[b][s]->cd(); // Clone() registers in the current directory
            hChargeCorrectedvsEta2[b][s] = (TH2F *)hChargeCorrectedvsEta[b][s]->Clone();
            hChargeCorrectedvsEta2[b][s]->SetName(Form("hClusterChargeCorrectedvsEta2_board_%d_side_%d", b, s));
            hChargeCorrectedvsEta2[b][s]->SetTitle(Form("Highest-charge cluster corrected charge vs eta (rebinned), board %d side %d", b, s));
            hChargeCorrectedvsEta2[b][s]->RebinX(rebinFactor);
        }
    }
}

// ### Helper: Merge Rebinned Histograms ###
// Sums hChargeCorrectedvsEta2 over all (board, side) into one TH2F, stored in output_file.
// Returns nullptr if there is nothing to merge.
TH2F *merge_rebinned_histograms(
    int NBoards,
    int NSides,
    const std::vector<std::vector<int>> &SkipBoards,
    std::vector<std::vector<TH2F *>> &hChargeCorrectedvsEta2,
    TFile *output_file)
{
    TH2F *hMerged = nullptr;
    for (int b = 0; b < NBoards; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s] || !hChargeCorrectedvsEta2[b][s])
                continue;

            if (!hMerged)
            {
                output_file->cd(); // Clone() registers in the current directory
                hMerged = (TH2F *)hChargeCorrectedvsEta2[b][s]->Clone("hClusterChargeCorrectedvsEta_merged");
                hMerged->SetTitle("Highest-charge cluster corrected charge vs eta (rebinned), all boards and sides");
            }
            else
            {
                hMerged->Add(hChargeCorrectedvsEta2[b][s]);
            }
        }
    }
    return hMerged;
}

// ### Helper: Perform Fits & Calculate Eta Energy Response ###
// Fits a gaussian to the corrected charge projection in each eta bin of the merged histogram.
void fit_eta_energy(
    TH2F *h2,
    TDirectory *dir,
    TGraphErrors *gEtaEnergy,
    std::vector<double> & etaCorr,
    std::vector<double> & etaCorrErrs)
{
     etaCorr.assign(h2->GetNbinsX(), std::nan(""));
     etaCorrErrs.assign(h2->GetNbinsX(), std::nan(""));

    for (int bin = 1; bin <= h2->GetNbinsX(); bin++)
    {
        TH1D *proj = h2->ProjectionY(Form("proj_merged_bin_%d", bin), bin, bin);
        proj->SetDirectory(dir);

        if (proj->GetEntries() < 100)
        {
            std::cout << "  bin " << bin << ": too few entries, skipped\n";
            continue;
        }

        // Peak search on bin projection, restricted to the known charge range,
        // so a single fluctuating bin or the low-charge tail cannot win.
        TH1D *sm = (TH1D *)proj->Clone(Form("sm_merged_bin_%d", bin));
        sm->SetDirectory(nullptr);
        sm->GetXaxis()->SetRangeUser(3.0, 15.0);
        double peak = sm->GetBinCenter(sm->GetMaximumBin());
        delete sm;
        double lo = std::max(0.0, peak - 0.3);
        double hi = peak + 0.3;

        TF1 *fG = new TF1(Form("fG_merged_bin_%d", bin), "gaus", lo, hi);
        fG->SetParNames("constant", "mean", "sigma");
        fG->SetParameters(proj->GetMaximum(), peak, 0.05);
        fG->SetParLimits(0, 1e-3, 2.0 * proj->GetMaximum());
        fG->SetParLimits(1, peak - 0.2, peak + 0.2);
        fG->SetParLimits(2, 0.001, 0.5);

        int status = proj->Fit(fG, "RQ");

        for (int p = 0; p < 3; p++)
        {
            double plo, phi;
            fG->GetParLimits(p, plo, phi);
            double v = fG->GetParameter(p);
            double tol = 1e-3 * (phi - plo);
            if (v - plo < tol || phi - v < tol)
                std::cout << "  bin " << bin << ": " << fG->GetParName(p)
                          << " AT LIMIT, value " << v
                          << " in [" << plo << ", " << phi << "]\n";
        }

        if (status != 0)
        {
            std::cout << "  bin " << bin << ": fit failed, status " << status << "\n";
            continue;
        }

        int n = gEtaEnergy->GetN();
        gEtaEnergy->SetPoint(n, h2->GetXaxis()->GetBinCenter(bin), fG->GetParameter(1));
        gEtaEnergy->SetPointError(n, 0.5 * h2->GetXaxis()->GetBinWidth(bin), fG->GetParError(1));

         etaCorr[bin - 1] = fG->GetParameter(1);
         etaCorrErrs[bin - 1] = fG->GetParError(1);
    }
}

// ### Helper: Save Eta Energy Correction ###
// Writes the table read by find_closest_correction() in read_clusters: eta, charge, correction.

// The reference is the fitted mean in the bin containing eta = 0.5, so every eta bin is scaled to match it.

// The correction depends on eta only. To make read_clusters' lookup (nearest point in eta AND charge)
// behave as a pure eta lookup, the table is written as a full rectangular (eta x charge) grid,
// with the same correction repeated for every charge value in a given eta bin.
void save_eta_energy_correction(
    TH2F *h2,
    const std::vector<double> & etaCorr,
    TString output_filename)
{
    int refBin = h2->GetXaxis()->FindBin(0.5);
    double reference =  etaCorr[refBin - 1];
    if (std::isnan(reference) || reference <= 0.0)
    {
        std::cerr << "No valid fit in reference bin " << refBin << " (eta = 0.5), correction file not written" << std::endl;
        return;
    }
    std::cout << "Reference charge at eta = 0.5 (bin " << refBin << "): " << reference << std::endl;

    std::ofstream output_file(output_filename);
    if (!output_file.is_open())
    {
        std::cerr << "Cannot open file " << output_filename << std::endl;
        return;
    }

    const int nCharge = 27; // charge axis 0..26, covers the 25.5 histogram limit

    output_file << "# eta charge correction\n"
                << std::setprecision(8);
    int nEta = 0;
    for (size_t i = 0; i <  etaCorr.size(); i++)
    {
        // Failed bins are dropped from the eta axis; the lookup falls back to the neighbouring bin
        if (std::isnan( etaCorr[i]) ||  etaCorr[i] <= 0.0)
            continue;

        double eta = h2->GetXaxis()->GetBinCenter(i + 1);
        double corr = reference /  etaCorr[i] / 10.0;
        for (int c = 0; c < nCharge; c++)
            output_file << eta << " " << c << " " << corr << "\n";
        nEta++;
    }
    std::cout << "Wrote " << nEta << " eta bins x " << nCharge << " charge values = "
              << nEta * nCharge << " rows" << std::endl;
}

// ### Helper: Write Histograms ###
void write_histograms(
    int NBoards,
    int NSides,
    const std::vector<std::vector<int>> &SkipBoards,
    std::vector<std::vector<TDirectory *>> &dirs,
    std::vector<std::vector<TH2F *>> &hChargevsEta,
    std::vector<std::vector<TH2F *>> &hChargeCorrectedvsEta,
    std::vector<std::vector<TH2F *>> &hChargeCorrectedvsEta2,
    TFile *output_file,
    TH2F *hMerged,
    TGraphErrors *gEtaEnergy)
{
    for (int b = 0; b < NBoards; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s] || !dirs[b][s])
                continue;

            dirs[b][s]->cd();
            hChargevsEta[b][s]->Write();
            hChargeCorrectedvsEta[b][s]->Write();
            hChargeCorrectedvsEta2[b][s]->Write();
        }
    }

    // Superimpose fitted means on the merged histogram
    output_file->cd();
    TCanvas *c = new TCanvas("cEtaEnergy_merged", "Eta energy response, all boards and sides", 900, 600);
    hMerged->Draw("COLZ");

    gEtaEnergy->SetMarkerStyle(20);
    gEtaEnergy->SetMarkerColor(kRed);
    gEtaEnergy->SetLineColor(kRed);
    gEtaEnergy->Draw("P SAME");

    c->Write();
    gEtaEnergy->Write();
    hMerged->Write();
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
    std::vector<std::vector<TH2F *>> hChargeCorrectedvsEta2(NBoards, std::vector<TH2F *>(NSides, nullptr));
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
        }
    }

    create_histograms(NBoards, NSides, SkipBoards, output_file, dirs, hChargevsEta, hChargeCorrectedvsEta);

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

    // 1. Fill Histograms
    fill_histograms(nEntries, NBoards, NSides, SkipBoards, minStrip, maxStrip, trees, clusters, cal, gain_table, hChargevsEta, hChargeCorrectedvsEta);

    // Setup re-binned histogram (1000 eta bins -> 100 bins)
    create_rebinned_histograms(NBoards, NSides, SkipBoards, dirs, hChargeCorrectedvsEta, hChargeCorrectedvsEta2, 10);

    // 2. Merge rebinned histograms of all boards and sides
    TH2F *hMerged = merge_rebinned_histograms(NBoards, NSides, SkipBoards, hChargeCorrectedvsEta2, output_file);
    if (!hMerged)
    {
        std::cerr << "No histograms to merge" << std::endl;
        return 1;
    }

    // 3. Perform Fits on the merged histogram
    TGraphErrors *gEtaEnergy = new TGraphErrors();
    gEtaEnergy->SetName("gEtaEnergy_merged");
    std::vector<double>  etaCorr,  etaCorrErrs;
    fit_eta_energy(hMerged, output_file, gEtaEnergy,  etaCorr,  etaCorrErrs);

    // 4. Save correction table (eta, charge, correction)
    save_eta_energy_correction(hMerged,  etaCorr, eta_energy_correction_file);

    // 5. Write Histograms
    write_histograms(NBoards, NSides, SkipBoards, dirs, hChargevsEta, hChargeCorrectedvsEta, hChargeCorrectedvsEta2, output_file, hMerged, gEtaEnergy);

    output_file->Write();
    output_file->Close();
    f->Close();

    return 0;
}