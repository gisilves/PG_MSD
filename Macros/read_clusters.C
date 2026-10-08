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
#include <utility>

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

// ### Correction table for eta charge correction ###

struct CorrectionEntry
{
    double eta;
    double sqrt_adc;
    double correction;
};

struct CorrectionTable
{
    CorrectionEntry *data;
    size_t size;
    size_t capacity;
    // Fast path, filled by build_grid(). grid[ie * adc_axis.size() + ia]
    std::vector<double> eta_axis, adc_axis, grid;
    bool is_grid = false;
};

static size_t nearest_idx(const std::vector<double> &ax, double x)
{
    auto it = std::lower_bound(ax.begin(), ax.end(), x);
    if (it == ax.begin()) return 0;
    if (it == ax.end()) return ax.size() - 1;
    size_t i = it - ax.begin();
    return (x - ax[i - 1] <= ax[i] - x) ? i - 1 : i;
}

static void build_grid(CorrectionTable *t)
{
    std::set<double> e, a;
    for (size_t i = 0; i < t->size; ++i)
    {
        e.insert(t->data[i].eta);
        a.insert(t->data[i].sqrt_adc);
    }
    t->eta_axis.assign(e.begin(), e.end());
    t->adc_axis.assign(a.begin(), a.end());
    t->grid.assign(t->eta_axis.size() * t->adc_axis.size(), NAN);

    for (size_t i = 0; i < t->size; ++i)
    {
        size_t ie = std::lower_bound(t->eta_axis.begin(), t->eta_axis.end(), t->data[i].eta) - t->eta_axis.begin();
        size_t ia = std::lower_bound(t->adc_axis.begin(), t->adc_axis.end(), t->data[i].sqrt_adc) - t->adc_axis.begin();
        t->grid[ie * t->adc_axis.size() + ia] = t->data[i].correction;
    }

    // Full grid: every cell filled, no duplicate points.
    t->is_grid = (t->size == t->grid.size()) &&
                 std::none_of(t->grid.begin(), t->grid.end(), [](double v) { return std::isnan(v); });
}

int load_correction_table(const char *filename, CorrectionTable *table)
{
    FILE *file = fopen(filename, "r");
    if (!file)
        return 0;

    table->capacity = 16;
    table->size = 0;
    table->data = (CorrectionEntry *)malloc(table->capacity * sizeof(CorrectionEntry));
    if (!table->data)
    {
        fclose(file);
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), file))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        CorrectionEntry entry;
        if (sscanf(line, "%lf,%lf,%lf", &entry.eta, &entry.sqrt_adc, &entry.correction) == 3 ||
            sscanf(line, "%lf %lf %lf", &entry.eta, &entry.sqrt_adc, &entry.correction) == 3)
        {
            if (table->size >= table->capacity)
            {
                table->capacity *= 2;
                CorrectionEntry *temp = (CorrectionEntry *)realloc(table->data, table->capacity * sizeof(CorrectionEntry));
                if (!temp)
                {
                    free(table->data);
                    fclose(file);
                    return 0;
                }
                table->data = temp;
            }
            table->data[table->size++] = entry;
        }
    }

    fclose(file);
    return 1;
}

// Correction using both eta and charge as lookup keys.
double find_closest_correction(const CorrectionTable *table, double eta, double sqrt_adc)
{
    if (!table || table->size == 0)
        return 1.0;

    if (table->is_grid)
        return table->grid[nearest_idx(table->eta_axis, eta) * table->adc_axis.size() +
                           nearest_idx(table->adc_axis, sqrt_adc)];

    // Fallback: if table has holes we use linear scan
    double min_dist_sq = DBL_MAX;
    double closest_correction = table->data[0].correction;

    for (size_t i = 0; i < table->size; ++i)
    {
        double d_eta = table->data[i].eta - eta;
        double d_adc = table->data[i].sqrt_adc - sqrt_adc;
        double dist_sq = d_eta * d_eta + d_adc * d_adc;

        if (dist_sq < min_dist_sq)
        {
            min_dist_sq = dist_sq;
            closest_correction = table->data[i].correction;
        }
    }

    return closest_correction;
}

// Eta-only correction with linear interpolation between eta points.
double find_eta_correction(const CorrectionTable *t, double eta)
{
    if (!t || t->size == 0)
        return 1.0;

    static std::vector<std::pair<double, double>> lut;
    if (lut.empty())
    {
        for (size_t i = 0; i < t->size; ++i)
            lut.emplace_back(t->data[i].eta, t->data[i].correction);
        std::sort(lut.begin(), lut.end());
    }

    if (eta <= lut.front().first)
        return lut.front().second;
    if (eta >= lut.back().first)
        return lut.back().second;

    auto hi = std::lower_bound(lut.begin(), lut.end(), std::make_pair(eta, -DBL_MAX));
    auto lo = hi - 1;
    double f = (eta - lo->first) / (hi->first - lo->first);
    return (lo->second + f * (hi->second - lo->second)) * 10.0;
}

void free_correction_table(CorrectionTable *table)
{
    if (table)
    {
        free(table->data);
        table->data = NULL;
        table->size = 0;
        table->capacity = 0;
    }
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

// ### Eta correction for position ###
const double XMIN = 0.0, XMAX = 150.0;

// par: 0-2 amplitudes (a,b,c), 3-5 steepness (d,e,f),
//      6-8 centers (g,h,i), 9 slope (j), 10 intercept (k)
double sigmoid(double x, double amp, double rate, double center)
{
    return amp / (1.0 + std::exp(-rate * (x - center)));
}

double raw(double x, const double *p)
{
    return sigmoid(x, p[0], p[3], p[6]) + sigmoid(x, p[1], p[4], p[7]) + sigmoid(x, p[2], p[5], p[8]) + p[9] * x + p[10];
}

// Raw model rescaled so that f(0) = 0 and f(1) = 1.
// x[0] is normalized to [0, 1]. Parameters stay in the original 0-150 units.
double model(double *x, double *p)
{
    const double y0 = raw(XMIN, p);
    const double y1 = raw(XMAX, p);
    const double xp = XMIN + x[0] * (XMAX - XMIN);
    return (raw(xp, p) - y0) / (y1 - y0);
}

// Function for position from eta function.
// The TF1 is built once and reused.
float positionFromEta(float strip, float pitch, float eta)
{
    static TF1 *fModel = nullptr;
    if (!fModel)
    {
        // Model for eta correction of position
        const double pars[11] = {
            -0.225, -0.238, -0.226,  // amplitudes
            -0.272, -0.276, -0.291,  // steepness
            25.748, 72.649, 120.081, // centers
            0.0014, 0.735            // linear trend: slope, intercept
        };

        fModel = new TF1("fModel", model, 0.0, 1.0, 11);
        fModel->SetParameters(pars);
        fModel->SetNpx(1000);
    }

    // Position is defined as: strip + pitch * F(eta)
    return strip + pitch * fModel->Eval(eta);
}

// ### Main function ###
int read_clusters(TString filename, TString output_filename, TString calibration_file, TString va_gains_file = "TAMSD_VA_gain.cal", TString eta_energy_correction_file = "TAMSD_Energy_Calibration.cal")
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

    // Load correction table for eta correction of charge
    CorrectionTable eta_energy_correction_table;
    if (!load_correction_table(eta_energy_correction_file, &eta_energy_correction_table))
    {
        std::cerr << "Cannot load correction table" << std::endl;
        return 1;
    }
    std::cout << "Loaded correction table with " << eta_energy_correction_table.size << " entries." << std::endl;

    bool use_eta_only_lookup = false;
    std::cout << "Do you want to use eta-only lookup? [y/n] ";
    char answer;
    std::cin >> answer;
    if (answer == 'y' || answer == 'Y')
    {
        use_eta_only_lookup = true;
        std::cout << "Eta-only lookup active. Correction factor at eta 0.05 / 0.5 / 0.95: "
                  << find_eta_correction(&eta_energy_correction_table, 0.05) << " / "
                  << find_eta_correction(&eta_energy_correction_table, 0.5) << " / "
                  << find_eta_correction(&eta_energy_correction_table, 0.95) << std::endl;
    }

    // Load VA gain table
    GainTable gain_table;
    if (!load_gain_table(va_gains_file, &gain_table))
    {
        std::cerr << "Cannot load VA gain table" << std::endl;
        return 1;
    }
    std::cout << "Loaded gain table with " << gain_table.size() << " entries." << std::endl;

    // --- Instantiate Global Histograms in Root Directory ---
    output_file->cd();

    TH2F *hBeamProfile2D = new TH2F("hBeamProfile2D", "Beam profile 2D", 100, -0.5, 639.5, 100, -0.5, 639.5);

    TH1F *hMeanCharge = new TH1F("hMeanClusterCharge",
                                 "Mean cluster charge over boards and sides", 1000, -0.5, 25.5);
    hMeanCharge->GetXaxis()->SetTitle("Mean Charge (arbitrary units)");

    TH1F *hAllClusterCharge = new TH1F("hAllClusterCharge",
                                       "All cluster charge", 1000, -0.5, 25.5);
    hAllClusterCharge->GetXaxis()->SetTitle("Charge");

    TH1F *hMeanChargeCorrected = new TH1F("hMeanClusterChargeCorrected",
                                          "Mean cluster corrected charge over boards and sides", 1000, -0.5, 25.5);
    hMeanChargeCorrected->GetXaxis()->SetTitle("Mean Corrected Charge (arbitrary units)");

    TH1F *hAllClusterChargeCorrected = new TH1F("hAllClusterChargeCorrected",
                                                "All cluster corrected charge", 1000, -0.5, 25.5);
    hAllClusterChargeCorrected->GetXaxis()->SetTitle("Corrected Charge");

    std::vector<std::vector<TTree *>> trees(NBoards, std::vector<TTree *>(NSides, nullptr));
    std::vector<std::vector<std::vector<cluster> *>> clusters(NBoards, std::vector<std::vector<cluster> *>(NSides, nullptr));

    // Per-board histograms
    std::vector<std::vector<TH1F *>> hCharge(NBoards, std::vector<TH1F *>(NSides, nullptr));
    std::vector<std::vector<TH1F *>> hPos(NBoards, std::vector<TH1F *>(NSides, nullptr));
    std::vector<std::vector<TH2F *>> hChargevsPos(NBoards, std::vector<TH2F *>(NSides, nullptr));
    std::vector<std::vector<TH1F *>> hEta(NBoards, std::vector<TH1F *>(NSides, nullptr));
    std::vector<std::vector<TH1F *>> hPositionDiff(NBoards, std::vector<TH1F *>(NSides, nullptr));
    std::vector<std::vector<TH2F *>> hChargevsEta(NBoards, std::vector<TH2F *>(NSides, nullptr));

    std::vector<std::vector<TH1F *>> hChargeCorrected(NBoards, std::vector<TH1F *>(NSides, nullptr));
    std::vector<std::vector<TH2F *>> hChargeCorrectedvsPos(NBoards, std::vector<TH2F *>(NSides, nullptr));
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
                free_correction_table(&eta_energy_correction_table);
                return 1;
            }
            trees[b][s]->SetBranchAddress("clusters", &clusters[b][s]);

            // Create subdirectory under output_file and cd into it BEFORE instantiating histograms
            std::string name = Form("board_%d_side_%d", b, s);
            dirs[b][s] = output_file->mkdir(name.c_str());
            dirs[b][s]->cd();

            // Raw
            hCharge[b][s] = new TH1F(Form("hClusterCharge_board_%d_side_%d", b, s),
                                     Form("Highest-charge cluster charge, board %d side %d", b, s),
                                     1000, -0.5, 25.5);
            hCharge[b][s]->GetXaxis()->SetTitle("Charge");

            hPos[b][s] = new TH1F(Form("hClusterPosition_board_%d_side_%d", b, s),
                                  Form("Highest-charge cluster position, board %d side %d", b, s),
                                  250, -0.5, 639.5);
            hPos[b][s]->GetXaxis()->SetTitle("Position");

            hChargevsPos[b][s] = new TH2F(Form("hClusterChargevsPos_board_%d_side_%d", b, s),
                                          Form("Highest-charge cluster charge vs position, board %d side %d", b, s),
                                          250, -0.5, 639.5, 1000, -0.5, 25.5);
            hChargevsPos[b][s]->GetXaxis()->SetTitle("Position");
            hChargevsPos[b][s]->GetYaxis()->SetTitle("Charge");

            hEta[b][s] = new TH1F(Form("hClusterEta_board_%d_side_%d", b, s),
                                  Form("Highest-charge cluster eta, board %d side %d", b, s),
                                  1000, 0, 1);

            hChargevsEta[b][s] = new TH2F(Form("hClusterChargevsEta_board_%d_side_%d", b, s),
                                          Form("Highest-charge cluster charge vs eta, board %d side %d", b, s),
                                          1000, 0, 1, 1000, -0.5, 25.5);
            hChargevsEta[b][s]->GetXaxis()->SetTitle("Eta");
            hChargevsEta[b][s]->GetYaxis()->SetTitle("Charge");

            // Comparison of COG and position with eta correction
            hPositionDiff[b][s] = new TH1F(Form("hClusterPositionDiff_board_%d_side_%d", b, s),
                                           Form("Highest-charge cluster delta position (COG - positionFromEta), board %d side %d", b, s),
                                           250, -10, 10);
            hPositionDiff[b][s]->GetXaxis()->SetTitle("Delta Position [strips]");

            // Corrected
            hChargeCorrected[b][s] = new TH1F(Form("hClusterChargeCorrected_board_%d_side_%d", b, s),
                                              Form("Highest-charge cluster corrected charge, board %d side %d", b, s),
                                              1000, -0.5, 25.5);
            hChargeCorrected[b][s]->GetXaxis()->SetTitle("Corrected Charge");

            hChargeCorrectedvsPos[b][s] = new TH2F(Form("hClusterChargeCorrectedvsPos_board_%d_side_%d", b, s),
                                                   Form("Highest-charge cluster corrected charge vs position, board %d side %d", b, s),
                                                   250, -0.5, 639.5, 1000, -0.5, 25.5);
            hChargeCorrectedvsPos[b][s]->GetXaxis()->SetTitle("Position");
            hChargeCorrectedvsPos[b][s]->GetYaxis()->SetTitle("Corrected Charge");

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
        if (i == 100000)
        {
            std::cout << "100000 entries processed" << std::endl;
            break;
        }
        display_progress(i + 1, nEntries);
        float cog_x = -1;
        float cog_y = -1;

        float chargeSum = 0;
        float chargeCorrectedSum = 0;
        int nWithCluster = 0;

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

                float raw_charge = GetClusterMIPCharge(best);
                float pos = GetClusterCOG(best);
                int nStripsInCluster = GetClusterADC(best).size();
                int leftStrip = GetClusterEtaLeftStrip(best, &cal);

                float eta = GetClusterEta(best, &cal);
                
                // Find and apply VA gain and eta energy correction
                double gain = find_gain(gain_table, b, s, pos);
                double gain_charge = raw_charge / gain;
                double corr_factor = 1.0;
                if (use_eta_only_lookup)
                {
                    corr_factor = find_eta_correction(&eta_energy_correction_table, eta);
                }
                else
                {
                    corr_factor = find_closest_correction(&eta_energy_correction_table, eta, gain_charge);
                }
                float corrected_charge = gain_charge * corr_factor;

                // Fill raw histograms
                hCharge[b][s]->Fill(raw_charge);
                hAllClusterCharge->Fill(raw_charge);
                hPos[b][s]->Fill(pos);

                // Fill position difference histogram (strip units, pitch = 1).
                // Skip 1-strip clusters: eta = 1 has no pair.
                if (nStripsInCluster > 1 && leftStrip != -999)
                {
                    float delta_pos = pos - positionFromEta(leftStrip, 1.0, eta);
                    hPositionDiff[b][s]->Fill(delta_pos);
                }

                hChargevsPos[b][s]->Fill(pos, raw_charge);
                hEta[b][s]->Fill(eta);
                hChargevsEta[b][s]->Fill(eta, raw_charge);

                // Fill corrected histograms
                hChargeCorrected[b][s]->Fill(corrected_charge);
                hAllClusterChargeCorrected->Fill(corrected_charge);
                hChargeCorrectedvsPos[b][s]->Fill(pos, corrected_charge);
                hChargeCorrectedvsEta[b][s]->Fill(eta, corrected_charge);

                chargeSum += raw_charge;
                chargeCorrectedSum += corrected_charge;
                nWithCluster++;

                if (b == beamBoard && s == 0)
                    cog_x = pos;

                if (b == beamBoard && s == 1)
                    cog_y = pos;
            }
        }

        if (cog_x != -1 && cog_y != -1)
            hBeamProfile2D->Fill(cog_x, cog_y);

        if (nWithCluster > 0)
        {
            float meanCharge = chargeSum / nWithCluster;
            float meanChargeCorrected = chargeCorrectedSum / nWithCluster;
            hMeanCharge->Fill(meanCharge);
            hMeanChargeCorrected->Fill(meanChargeCorrected);
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
            hCharge[b][s]->Write();
            hChargeCorrected[b][s]->Write();
            hPos[b][s]->Write();
            hChargevsPos[b][s]->Write();
            hChargeCorrectedvsPos[b][s]->Write();
            hEta[b][s]->Write();
            hChargevsEta[b][s]->Write();
            hChargeCorrectedvsEta[b][s]->Write();
            hPositionDiff[b][s]->Write();
        }
    }

    // Write global histograms to the root directory
    output_file->cd();
    hMeanCharge->Write();
    hMeanChargeCorrected->Write();
    hAllClusterCharge->Write();
    hAllClusterChargeCorrected->Write();
    hBeamProfile2D->Write();

    free_correction_table(&eta_energy_correction_table);

    output_file->Write();
    output_file->Close();
    f->Close();

    return 0;
}