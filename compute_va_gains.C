#include <TFile.h>
#include <TTree.h>
#include <TSystem.h>
#include <vector>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cfloat>

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

int npInt = 100;

double langaufun(double *x, double *par) // not used atm
{
    double sc = 5.0;

    double xx, mpc, fland, sum = 0.0, xlow, xupp, step;
    double eta = par[0], mpv = par[1], area = par[2], sigma = par[3];

    mpc = mpv - 0.22278298 * eta;

    xlow = x[0] - sc * sigma;
    xupp = x[0] + sc * sigma;
    step = (xupp - xlow) / npInt;

    for (int i = 1; i <= npInt / 2; i++)
    {
        xx = xlow + (i - 0.5) * step;
        fland = TMath::Landau(xx, mpc, eta) / eta;
        sum += fland * TMath::Gaus(x[0], xx, sigma);

        xx = xupp - (i - 0.5) * step;
        fland = TMath::Landau(xx, mpc, eta) / eta;
        sum += fland * TMath::Gaus(x[0], xx, sigma);
    }

    return area * step * sum / (std::sqrt(2 * TMath::Pi()) * sigma);
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
    std::vector<std::vector<TH2F *>> &hChargevsPos)
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

                float eta = GetClusterEta(best, &cal);
                float raw_charge = GetClusterMIPCharge(best);

                // Skip 1-strip clusters
                if (GetClusterADC(best).size() == 1)
                    continue;

                if (eta >= 0.33 && eta <= 0.66)
                    hChargevsPos[b][s]->Fill(GetClusterCOG(best), raw_charge);
            }
        }
    }
    std::cout << std::endl;
}

// ### Helper: Perform Fits & Calculate VA Gains ###
void fit_va_gains(
    int NBoards,
    int NSides,
    const std::vector<std::vector<int>> &SkipBoards,
    std::vector<std::vector<TH2F *>> &hChargevsPos2,
    std::vector<std::vector<TGraphErrors *>> &gVAGain,
    std::vector<std::vector<std::vector<double>>> &vaGains,
    std::vector<std::vector<std::vector<double>>> &vaGainErrs,
    TFile *output_file)
{
    for (int b = 0; b < NBoards; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s])
                continue;

            gVAGain[b][s] = new TGraphErrors();
            gVAGain[b][s]->SetName(Form("gVAGain_board_%d_side_%d", b, s));

            TH2F *h2 = hChargevsPos2[b][s];
            vaGains[b][s].assign(h2->GetNbinsX(), std::nan(""));
            vaGainErrs[b][s].assign(h2->GetNbinsX(), std::nan(""));
            std::cout << "Board " << b << " side " << s << "\n";

            for (int bin = 1; bin <= h2->GetNbinsX(); bin++)
            {
                TH1D *proj = h2->ProjectionY(Form("proj_board_%d_side_%d_bin_%d", b, s, bin), bin, bin);
                proj->SetDirectory(output_file);

                if (proj->GetEntries() < 100)
                {
                    std::cout << "  bin " << bin << ": too few entries, skipped\n";
                    continue;
                }

                double peak = proj->GetBinCenter(proj->GetMaximumBin());
                double lo = std::max(0.0, peak - 0.3);
                double hi = peak + 0.3;

                TF1 *fG = new TF1(Form("fG_board_%d_side_%d_bin_%d", b, s, bin),
                                  "gaus", lo, hi);
                fG->SetParNames("constant", "mean", "sigma");
                fG->SetParameters(proj->GetMaximum(), peak, 0.05);
                fG->SetParLimits(0, 1e-3, 2.0 * proj->GetMaximum());
                fG->SetParLimits(1, peak - 0.2, peak + 0.2);
                fG->SetParLimits(2, 0.001, 0.5);

                int status = proj->Fit(fG, "RV");

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

                int n = gVAGain[b][s]->GetN();
                gVAGain[b][s]->SetPoint(n, h2->GetXaxis()->GetBinCenter(bin), fG->GetParameter(1));
                gVAGain[b][s]->SetPointError(n, 0.5 * h2->GetXaxis()->GetBinWidth(bin), fG->GetParError(1));

                vaGains[b][s][bin - 1] = fG->GetParameter(1);
                vaGainErrs[b][s][bin - 1] = fG->GetParError(1);
            }
        }
    }

    // Normalize vaGains to the first VA
    double normFactor = vaGains[0][0][0];
    if (normFactor != 0.0)
    {
        for (auto &board : vaGains)
        {
            for (auto &side : board)
            {
                for (auto &val : side)
                {
                    val /= normFactor;
                }
            }
        }
    }
}

// ### Helper: Save VA Gains to csv as Board, Side, Bin, Gain ###
void save_va_gains(
    int NBoards,
    int NSides,
    const std::vector<std::vector<int>> &SkipBoards,
    const std::vector<std::vector<std::vector<double>>> &vaGains,
    TString output_filename)
{
    std::ofstream output_file(output_filename);
    if (!output_file.is_open())
    {
        std::cerr << "Cannot open file " << output_filename << std::endl;
        return;
    }

    output_file << "# Detector VA Correction\n";
    for (int b = 0; b < NBoards; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s])
                continue;
            for (size_t i = 0; i < vaGains[b][s].size(); i++)
            {
                output_file << 2* b + s << " " << i << " " << vaGains[b][s][i] << "\n";
            }
        }
    }
}

// ### Helper: Print VA Gains ###
void print_va_gains(
    int NBoards,
    int NSides,
    const std::vector<std::vector<int>> &SkipBoards,
    const std::vector<std::vector<std::vector<double>>> &vaGains,
    const std::vector<std::vector<std::vector<double>>> &vaGainErrs)
{
    std::cout << "VA gains:\n";
    for (int b = 0; b < NBoards; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s])
                continue;
            std::cout << "Board " << b << " side " << s << "\n";
            for (size_t i = 0; i < vaGains[b][s].size(); i++)
            {
                std::cout << "  bin " << i + 1 << ": " << vaGains[b][s][i] << " +/- " << vaGainErrs[b][s][i] << "\n";
            }
        }
    }
}

// ### Main function ###
int compute_va_gains(TString filename, TString output_filename, TString calibration_file)
{
    gSystem->Load("./libcluster.so");

    const int NBoards = 3;
    const int NSides = 2;

    const std::vector<std::vector<int>> SkipBoards = {{0, 0}, {0, 0}, {0, 0}};

    const std::vector<std::vector<int>> minStrip = {{0, 0}, {0, 0}, {0, 0}};
    const std::vector<std::vector<int>> maxStrip = {{639, 639}, {639, 639}, {639, 639}};

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

    std::vector<std::vector<TTree *>> trees(NBoards, std::vector<TTree *>(NSides, nullptr));
    std::vector<std::vector<std::vector<cluster> *>> clusters(NBoards, std::vector<std::vector<cluster> *>(NSides, nullptr));

    // Histograms
    std::vector<std::vector<TH2F *>> hChargevsPos(NBoards, std::vector<TH2F *>(NSides, nullptr));
    std::vector<std::vector<TH2F *>> hChargevsPos2(NBoards, std::vector<TH2F *>(NSides, nullptr));

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

            hChargevsPos[b][s] = new TH2F(Form("hClusterChargevsPos_board_%d_side_%d", b, s),
                                          Form("Highest-charge cluster charge vs position, board %d side %d", b, s),
                                          1000, -0.5, 639.5, 1000, -0.5, 25.5);
            hChargevsPos[b][s]->GetXaxis()->SetTitle("Position");
            hChargevsPos[b][s]->GetYaxis()->SetTitle("Charge");
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

    // 1. Fill Histograms
    fill_histograms(nEntries, NBoards, NSides, SkipBoards, minStrip, maxStrip, trees, clusters, cal, hChargevsPos);

    // Setup re-binned histograms for VA gain calculations
    for (int b = 0; b < NBoards; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s])
                continue;
            hChargevsPos2[b][s] = (TH2F *)hChargevsPos[b][s]->Clone();
            hChargevsPos2[b][s]->SetName(Form("hClusterChargevsPos2_board_%d_side_%d", b, s));
            hChargevsPos2[b][s]->SetTitle(Form("Highest-charge cluster charge vs position, board %d side %d", b, s));
            hChargevsPos2[b][s]->RebinX(100);
        }
    }

    std::vector<std::vector<TGraphErrors *>> gVAGain(NBoards, std::vector<TGraphErrors *>(NSides, nullptr));
    std::vector<std::vector<std::vector<double>>> vaGains(NBoards, std::vector<std::vector<double>>(NSides));
    std::vector<std::vector<std::vector<double>>> vaGainErrs(NBoards, std::vector<std::vector<double>>(NSides));

    // 2. Perform Fits & Calculate VA Gains
    fit_va_gains(NBoards, NSides, SkipBoards, hChargevsPos2, gVAGain, vaGains, vaGainErrs, output_file);

    // 3. Print VA Gains
    print_va_gains(NBoards, NSides, SkipBoards, vaGains, vaGainErrs);

    // 4. Save VA Gains to csv
    save_va_gains(NBoards, NSides, SkipBoards, vaGains, "va_gains.csv");

    // Superimpose VA gains on hChargevsPos2
    output_file->cd();
    for (int b = 0; b < NBoards; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s])
                continue;

            TCanvas *c = new TCanvas(Form("cVAGain_board_%d_side_%d", b, s),
                                     Form("VA gains, board %d side %d", b, s), 900, 600);
            hChargevsPos2[b][s]->Draw("COLZ");

            gVAGain[b][s]->SetMarkerStyle(20);
            gVAGain[b][s]->SetMarkerColor(kRed);
            gVAGain[b][s]->SetLineColor(kRed);
            gVAGain[b][s]->Draw("P SAME");

            c->Write();
            gVAGain[b][s]->Write();
        }
    }

    // Writing histograms
    output_file->cd();
    for (int b = 0; b < NBoards; b++)
    {
        for (int s = 0; s < NSides; s++)
        {
            if (SkipBoards[b][s])
                continue;
            hChargevsPos[b][s]->Write();
            hChargevsPos2[b][s]->Write();
        }
    }

    output_file->Write();
    output_file->Close();
    f->Close();

    return 0;
}