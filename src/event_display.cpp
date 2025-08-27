///////////////////////////////////////
// Simple interactive event display   //
// Shows ADC amplitude vs triggered   //
// channel per event, with Next/Prev  //
// buttons to toggle events.          //
///////////////////////////////////////

#include "TFile.h"
#include "TTree.h"
#include "TGraph.h"
#include "TCanvas.h"
#include "TButton.h"
#include "TApplication.h"
#include "TStyle.h"
#include "TSystem.h"
#include "TAxis.h"
#include "TPad.h"
#include "TH1.h"
#include "TInterpreter.h"
#include "TLegend.h"

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include "CmdLineParser.h"
#include "Logger.h"
#include "ocaEvent.h"
#include "constants.h"

LoggerInit([]{
  Logger::getUserHeader() << "[" << FILENAME << "]";
});

// Global state used by the buttons callbacks
namespace {
    const int kNDet = oca::N_DETECTORS;
    const int kNChan = oca::N_CHANNELS;

    std::vector<TTree*> g_rawTrees;
    std::vector<std::vector<float>> g_baseline;
    std::vector<std::vector<float>> g_sigma;
    std::vector<std::vector<bool>>  g_mask;   // true => masked/bad channel
    int g_nSigma = 7; // default, overridden by CLI
    int g_nEntries = 0;
    int g_evt = 0;
    bool g_useMask = true; // ON by default

    // One graph per detector
    std::vector<TGraph*> g_graphs;
    std::vector<TLegend*> g_legends; // one legend per detector pad
    TCanvas* g_canvas = nullptr;
    TPad* g_padCtrl = nullptr; // control buttons pad (left column)
    TPad* g_padGrid = nullptr; // plot pad (right area)
    // Forward declaration for Next callback (used by buttons/console)
    void NextEventImpl();
    void PrevEventImpl();

    // Small helper to read and fill an Event for the given entry
    bool fillEvent(int entry, Event& ev) {
        if (g_rawTrees.size() < (size_t)kNDet) return false;
        // Branch data holders
        static std::vector<std::vector<float>*> data;
        if (data.empty()) {
            data.reserve(kNDet);
            for (int i=0;i<kNDet;i++) data.emplace_back(new std::vector<float>());
            // Only 3 detectors are used: 0->raw_events, 1->raw_events_B, 2->raw_events_C
            g_rawTrees.at(0)->SetBranchAddress("RAW Event",   &data[0]);
            g_rawTrees.at(1)->SetBranchAddress("RAW Event B", &data[1]);
            g_rawTrees.at(2)->SetBranchAddress("RAW Event C", &data[2]);
        }
        for (int i=0;i<kNDet;i++) data[i]->clear();

        for (int det=0; det<kNDet; ++det) {
            if (!g_rawTrees[det]) continue;
            g_rawTrees[det]->GetEntry(entry);
            ev.AddPeak(det, *data[det]);
        }
        return true;
    }

    // Check if event has at least one unmasked triggered hit
    bool hasUnmaskedHits(int entry) {
        Event ev;
    ev.nDetectors = kNDet;
    ev.nChannels  = kNChan;
        ev.SetBaseline(g_baseline);
        ev.SetSigma(g_sigma);
        ev.SetNSigma(g_nSigma);
        if (!fillEvent(entry, ev)) return false;
        ev.ExtractTriggeredHits();
        for (auto &hit : ev.GetTriggeredHits()) {
            int det = hit.first;
            int ch  = hit.second;
            if (det < 0 || det >= kNDet || ch < 0 || ch >= kNChan) continue;
            if (g_useMask && g_mask[det][ch]) continue;
            return true; // found at least one valid hit
        }
        return false;
    }

    void updateDisplay() {
    if (!g_canvas || !g_padGrid) return;
    g_canvas->cd();
    g_canvas->SetTitle(Form("Event %d: ADC vs Triggered Channel", g_evt));

    Event ev;
    ev.nDetectors = kNDet;
    ev.nChannels  = kNChan;
        ev.SetBaseline(g_baseline);
        ev.SetSigma(g_sigma);
        ev.SetNSigma(g_nSigma);
        if (!fillEvent(g_evt, ev)) return;
        ev.ExtractTriggeredHits();

        // Prepare per-detector vectors of (ch, amp)
        std::vector<std::vector<std::pair<int,float>>> perDet(kNDet);
        std::vector<double> yMaxDet(kNDet, 1.0);
        for (auto &hit : ev.GetTriggeredHits()) {
            int det = hit.first;
            int ch  = hit.second;
            if (det<0 || det>=kNDet || ch<0 || ch>=kNChan) continue;
            if (g_useMask && g_mask[det][ch]) continue; // skip masked channels
            float amp = ev.GetPeak(det, ch) - ev.GetBaseline(det, ch);
            perDet[det].push_back({ch, amp});
            if (amp > yMaxDet[det]) yMaxDet[det] = amp;
        }

        // Update graphs
        for (int det=0; det<kNDet; ++det) {
            TGraph* g = g_graphs[det];
            g->Set(0); // clear points
            int idx = 0;
            for (auto &p : perDet[det]) {
                g->SetPoint(idx++, p.first, p.second);
            }
            g->SetTitle(Form("Det %d: ADC vs Channel (Event %d)", det, g_evt));
        }

        // Draw only inside the grid pad so buttons remain
        g_padGrid->cd();
        for (int det=0; det<kNDet; ++det) {
            g_padGrid->cd(det+1);
            gPad->Clear();
            gPad->SetGrid();
            gPad->SetLeftMargin(0.12);
            gPad->SetRightMargin(0.03);
            gPad->SetBottomMargin(0.16);
            gPad->SetTopMargin(0.08);
            // Draw frame first to avoid errors with empty graphs
            auto frame = gPad->DrawFrame(0.0, 0.0, (double)kNChan, yMaxDet[det] * 1.1);
            auto xax = frame->GetXaxis();
            auto yax = frame->GetYaxis();
            xax->SetTitle("Channel");
            yax->SetTitle("ADC amplitude");
            xax->SetTitleSize(0.055);
            yax->SetTitleSize(0.055);
            xax->SetLabelSize(0.045);
            yax->SetLabelSize(0.045);
            g_graphs[det]->SetMarkerStyle(20);
            g_graphs[det]->SetMarkerSize(1.2);
            if (g_graphs[det]->GetN() > 0) {
                g_graphs[det]->Draw("P SAME");
            }
            // Bottom-left, smaller legend per pad
            if (g_legends.size() != (size_t)kNDet) g_legends.resize(kNDet, nullptr);
            if (!g_legends[det]) {
                g_legends[det] = new TLegend(0.15, 0.12, 0.45, 0.25);
                g_legends[det]->SetBorderSize(0);
                g_legends[det]->SetFillStyle(0);
                g_legends[det]->SetTextSize(0.035);
            } else {
                g_legends[det]->Clear();
            }
            g_legends[det]->AddEntry(g_graphs[det], Form("Det %d hits", det), "p");
            g_legends[det]->Draw();
        }
        g_canvas->Modified();
        g_canvas->Update();
    }

    // Button callbacks implementation (internal)
    void NextEventImpl() {
        if (g_nEntries<=0) return;
        // advance until a non-empty (after mask) event is found, or after full loop
        int tries = 0;
        do {
            g_evt = (g_evt + 1) % g_nEntries;
            tries++;
        } while (tries < g_nEntries && !hasUnmaskedHits(g_evt));
        updateDisplay();
    }
    void PrevEventImpl() {
        if (g_nEntries<=0) return;
        int tries = 0;
        do {
            g_evt = (g_evt - 1);
            if (g_evt < 0) g_evt = g_nEntries - 1;
            tries++;
        } while (tries < g_nEntries && !hasUnmaskedHits(g_evt));
        updateDisplay();
    }
    // No Prev in simplified UI
}

// Expose callbacks to Cling with C linkage to avoid name mangling issues
extern "C" void NextEvent() { ::NextEventImpl(); }
extern "C" void PrevEvent() { ::PrevEventImpl(); }

int main(int argc, char* argv[]) {
    CmdLineParser clp;
    clp.getDescription() << "> Interactive display: ADC vs triggered channels per event" << std::endl;
    clp.addDummyOption("Options");
    clp.addOption("inputRootFile",  {"-r", "--root-file"}, "Input ROOT file");
    clp.addOption("inputCalFile",   {"-c", "--cal-file"},  "Calibration file (.cal)");
    clp.addOption("nSigma",         {"-s", "--n-sigma"},   "NSigma threshold");
    clp.addOption("runNumber",      {"-n", "--run-number"}, "Run number (for titles)");
    clp.addTriggerOption("noMask",  {"--no-mask"},           "Disable masking of bad channels (from calib)" );
    clp.addDummyOption();

    LogInfo << clp.getDescription().str() << std::endl;
    LogInfo << "Usage:" << std::endl;
    LogInfo << clp.getConfigSummary() << std::endl;
    clp.parseCmdLine(argc, argv);
    LogThrowIf(clp.isNoOptionTriggered(), "No option was provided.");
    LogInfo << "Provided arguments:\n" << clp.getValueSummary() << std::endl;

    std::string calPath = clp.getOptionVal<std::string>("inputCalFile");
    std::string rootPath = clp.getOptionVal<std::string>("inputRootFile");
    g_nSigma = clp.getOptionVal<int>("nSigma");
    std::string runNumber = clp.isOptionTriggered("runNumber") ? clp.getOptionVal<std::string>("runNumber") : std::string("");
    g_useMask = !clp.isOptionTriggered("noMask");

    // 1) Read calibration
    LogInfo << "Reading calibration file: " << calPath << std::endl;
    std::ifstream calFile(calPath);
    LogThrowIf(!calFile.is_open(), "Calibration file could not be opened");
    g_baseline.assign(kNDet, std::vector<float>(kNChan, 0.0f));
    g_sigma.assign(kNDet, std::vector<float>(kNChan, 0.0f));
    g_mask.assign(kNDet, std::vector<bool>(kNChan, false));
    std::string line;
    for (int det=0; det<kNDet; ++det) {
        for (int i=0;i<18;i++) std::getline(calFile, line); // skip header
        for (int ch=0; ch<kNChan; ++ch) {
            if (!std::getline(calFile, line)) {
                LogError << "Unexpected end of calibration file" << std::endl; return 1;
            }
            std::istringstream iss(line);
            std::vector<float> vals; vals.reserve(8);
            std::string tok; float v;
            while (std::getline(iss, tok, ',')) { std::istringstream iv(tok); if (iv>>v) vals.push_back(v); }
            if (vals.size()!=8) { LogError << "Bad calib line: size=" << vals.size() << std::endl; return 1; }
            int channel = (int)vals[0];
            g_baseline[det][channel] = vals[3];
            g_sigma[det][channel]    = vals[5];
            int badflag = (int)vals[6]; // 0 good, 1 bad
            g_mask[det][channel]     = (badflag != 0);
        }
    }

    // 2) Open ROOT and trees
    LogInfo << "Opening ROOT file: " << rootPath << std::endl;
    TFile* f = TFile::Open(rootPath.c_str(), "READ");
    LogThrowIf(!f || !f->IsOpen(), "ROOT file not open");
    g_rawTrees.clear(); g_rawTrees.reserve(kNDet);
    g_rawTrees.emplace_back( (TTree*)f->Get("raw_events") );
    g_rawTrees.emplace_back( (TTree*)f->Get("raw_events_B") );
    g_rawTrees.emplace_back( (TTree*)f->Get("raw_events_C") );
    // Basic validity check
    for (int i=0;i<kNDet;i++) {
        LogThrowIf(!g_rawTrees[i], Form("Missing tree for detector %d", i));
    }
    g_nEntries = g_rawTrees[0] ? g_rawTrees[0]->GetEntries() : 0;
    LogThrowIf(g_nEntries<=0, "No entries found in raw_events tree");

    // 3) ROOT app and display
    TApplication app("evtDisplay", &argc, argv);
    gStyle->SetOptStat(0);

    g_canvas = new TCanvas("c_evt", Form("ADC vs Triggered Channels%s%s", runNumber.empty()?"":" (Run ", runNumber.empty()?"":" )"), 1280, 900);

    // No interpreter prototype needed when calling via function-pointer string

    // Create a left control pad (wider for large buttons) and a main grid pad on the right
    g_padCtrl = new TPad("pCtrl", "controls", 0.0, 0.0, 0.22, 1.0);
    g_padCtrl->SetMargin(0.05,0.05,0.05,0.05);
    g_padCtrl->SetFillColor(kWhite);
    g_padCtrl->Draw();

    g_padGrid = new TPad("pGrid", "plots", 0.22, 0.0, 1.0, 1.0);
    g_padGrid->SetMargin(0.08,0.04,0.16,0.08);
    g_padGrid->Draw();
    g_padGrid->cd();
    // Arrange as 1x3 since we use 3 detectors
    g_padGrid->Divide(1,3);

    // Create graphs
    g_graphs.clear(); g_graphs.reserve(kNDet);
    g_legends.clear(); g_legends.reserve(kNDet);
    for (int det=0; det<kNDet; ++det) {
        auto g = new TGraph();
        g->SetName(Form("g_det_%d", det));
    g->SetMarkerStyle(20);
    g->SetMarkerSize(1.2);
        g_graphs.emplace_back(g);
    }

    // Buttons on the control pad (NDC of pad)
    g_padCtrl->cd();
    std::string prevCmd = Form("((void(*)())%p)()", (void*)(&PrevEventImpl));
    // Large vertical buttons: Prev bottom half, Next top half
    auto bPrev = new TButton("Prev", prevCmd.c_str(), 0.10, 0.08, 0.90, 0.46);
    bPrev->SetFillColor(kGray);
    bPrev->SetTextSize(0.25);
    bPrev->Draw();

    std::string nextCmd = Form("((void(*)())%p)()", (void*)(&NextEventImpl));
    auto bNext = new TButton("Next", nextCmd.c_str(), 0.10, 0.54, 0.90, 0.92);
    bNext->SetFillColor(kGray);
    bNext->SetTextSize(0.25);
    bNext->Draw();

    // Initial draw
    g_evt = 0;
    // Find first non-empty event considering mask
    if (g_nEntries > 0 && !hasUnmaskedHits(g_evt)) {
        for (int i=1; i<g_nEntries; ++i) {
            if (hasUnmaskedHits(i)) { g_evt = i; break; }
        }
    }
    updateDisplay();

    // Run interactive loop
    app.Run();
    return 0;
}
