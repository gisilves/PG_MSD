#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include "TFile.h"
#include "TTree.h"
#include "TBranch.h"
#include "TH1F.h"
#include "TH1I.h"
#include "TH2F.h"
#include "TCanvas.h"
#include "TGraph.h"
#include "TProfile.h"
#include "TApplication.h"
#include "TStyle.h"
#include "TSystem.h"
#include "TMarker.h"
#include "TLatex.h"
#include "TLegend.h"
#include "TEllipse.h"
#include "TF1.h"
#include "TPaveStats.h"
#include "TF2.h"
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <map>
#include <array>

#include <nlohmann/json.hpp>

#include "CmdLineParser.h"
#include "Logger.h"

LoggerInit([]{
  Logger::getUserHeader() << "[" << FILENAME << "]";
});

// A more robust way to get the base name, removing known suffixes
std::string GetBaseName(std::string const & path) {
    std::string base = path.substr(path.find_last_of("/\\") + 1);
    static const std::vector<std::string> suffixes = {"_clusters.root", ".root"};
    for (const auto& suffix : suffixes) {
        if (base.size() > suffix.size() && base.substr(base.size() - suffix.size()) == suffix) {
            return base.substr(0, base.size() - suffix.size());
        }
    }
    return base;
}

// Function to check if a channel is an edge channel (chip boundaries)
bool ChannelMask(int channel) {
    // Edge channels are at chip boundaries: 0, 63, 64, 127, 128, 191, 192, 255, 256, 319, 320, 383
    // Each chip has 64 channels, so edge channels are: 0, 63 of each chip
    // But user specified 0,1,62,63 and multiples, so including adjacent
    int chipNumber = channel / 64;
    int channelInChip = channel % 64;
    return (channelInChip == 0 || channelInChip == 1 || channelInChip == 62 || channelInChip == 63);
}

int main(int argc, char* argv[]) {
    CmdLineParser clp;

    clp.getDescription() << "> This program reads a clusters ROOT file and generates a PDF report." << std::endl;

    clp.addDummyOption("Main options");
    clp.addOption("inputRootFile", {"-i", "--input"}, "Input clusters ROOT file");
    clp.addOption("outputDir", {"-o", "--output"}, "Output directory for PDF report");
    clp.addOption("nSigma", {"-s", "--n-sigma"}, "Number of sigmas used for clustering (for labeling only)");

    clp.addDummyOption("Triggers");
    clp.addTriggerOption("verboseMode", {"-v"}, "Run in verbose mode");

    clp.addDummyOption();

    LogInfo << clp.getDescription().str() << std::endl;
    LogInfo << "Usage: " << std::endl;
    LogInfo << clp.getConfigSummary() << std::endl << std::endl;

    clp.parseCmdLine(argc, argv);

    LogThrowIf(clp.isNoOptionTriggered(), "No option was provided.");

    LogInfo << "Provided arguments: " << std::endl;
    LogInfo << clp.getValueSummary() << std::endl << std::endl;

    bool verbose = clp.isOptionTriggered("verboseMode");

    std::string inputFile = clp.getOptionVal<std::string>("inputRootFile");
    std::string outputDir = clp.getOptionVal<std::string>("outputDir");
    int nSigma = 0;
    if (clp.isOptionTriggered("nSigma")) {
        nSigma = clp.getOptionVal<int>("nSigma");
    }

    LogInfo << "Input file: " << inputFile << std::endl;
    LogInfo << "Output directory: " << outputDir << std::endl;

    // Open input file
    TFile *inputRootFile = new TFile(inputFile.c_str(), "READ");
    if (!inputRootFile->IsOpen()) {
        LogError << "Error: cannot open input file " << inputFile << std::endl;
        return 1;
    }

    // Get cluster tree
    TTree *clusterTree = (TTree*)inputRootFile->Get("clusters");
    if (!clusterTree) {
        LogError << "Error: cannot find clusters tree" << std::endl;
        return 1;
    }

    // Get event_info tree
    TTree *eventInfoTree = (TTree*)inputRootFile->Get("event_info");
    if (!eventInfoTree) {
        LogError << "Error: cannot find event_info tree" << std::endl;
        return 1;
    }

    // Get detector trees
    const int nDetectors = 4;
    const int nChannels = 384;
    TTree *detectorTrees[nDetectors];
    std::vector<float> *detectorData[nDetectors];
    for (int d = 0; d < nDetectors; ++d) {
        detectorTrees[d] = (TTree*)inputRootFile->Get(Form("detector%d", d));
        if (!detectorTrees[d]) {
            LogError << "Error: cannot find detector" << d << " tree" << std::endl;
            return 1;
        }
        detectorData[d] = new std::vector<float>();
        detectorTrees[d]->SetBranchAddress("data", &detectorData[d]);
    }

    // Get raw detector trees
    TTree *rawDetectorTrees[nDetectors];
    std::vector<float> *rawDetectorData[nDetectors];
    for (int d = 0; d < nDetectors; ++d) {
        rawDetectorTrees[d] = (TTree*)inputRootFile->Get(Form("raw_detector%d", d));
        if (!rawDetectorTrees[d]) {
            LogError << "Error: cannot find raw_detector" << d << " tree" << std::endl;
            return 1;
        }
        rawDetectorData[d] = new std::vector<float>();
        rawDetectorTrees[d]->SetBranchAddress("raw_data", &rawDetectorData[d]);
    }

    // Get sigma trees
    TTree *sigmaTrees[nDetectors];
    std::vector<float> *sigmaData[nDetectors];
    for (int d = 0; d < nDetectors; ++d) {
        sigmaTrees[d] = (TTree*)inputRootFile->Get(Form("sigma%d", d));
        if (!sigmaTrees[d]) {
            LogError << "Error: cannot find sigma" << d << " tree" << std::endl;
            return 1;
        }
        sigmaData[d] = new std::vector<float>();
        sigmaTrees[d]->SetBranchAddress("sigma", &sigmaData[d]);
        sigmaTrees[d]->GetEntry(0); // sigmas are static
    }

    // Get pedestal trees
    TTree *pedestalTrees[nDetectors];
    std::vector<float> *pedestalData[nDetectors];
    for (int d = 0; d < nDetectors; ++d) {
        pedestalTrees[d] = (TTree*)inputRootFile->Get(Form("pedestal%d", d));
        if (!pedestalTrees[d]) {
            LogError << "Error: cannot find pedestal" << d << " tree" << std::endl;
            return 1;
        }
        pedestalData[d] = new std::vector<float>();
        pedestalTrees[d]->SetBranchAddress("pedestal", &pedestalData[d]);
        pedestalTrees[d]->GetEntry(0); // pedestals are static
    }

    // Set up cluster branches
    std::vector<int> *cluster_detectors = nullptr;
    std::vector<int> *cluster_start_ch = nullptr;
    std::vector<int> *cluster_end_ch = nullptr;
    std::vector<int> *cluster_sizes = nullptr;
    std::vector<float> *cluster_amplitudes = nullptr;

    clusterTree->SetBranchAddress("detector", &cluster_detectors);
    clusterTree->SetBranchAddress("start_ch", &cluster_start_ch);
    clusterTree->SetBranchAddress("end_ch", &cluster_end_ch);
    clusterTree->SetBranchAddress("size", &cluster_sizes);
    clusterTree->SetBranchAddress("amplitude", &cluster_amplitudes);

    // Set up event_info branches
    Long64_t evt_size, fw_version, trigger_number, board_id, timestamp, ext_timestamp, trigger_id, file_offset;
    Int_t event_index;
    eventInfoTree->SetBranchAddress("event_index", &event_index);
    eventInfoTree->SetBranchAddress("evt_size", &evt_size);
    eventInfoTree->SetBranchAddress("fw_version", &fw_version);
    eventInfoTree->SetBranchAddress("trigger_number", &trigger_number);
    eventInfoTree->SetBranchAddress("board_id", &board_id);
    eventInfoTree->SetBranchAddress("timestamp", &timestamp);
    eventInfoTree->SetBranchAddress("ext_timestamp", &ext_timestamp);
    eventInfoTree->SetBranchAddress("trigger_id", &trigger_id);
    eventInfoTree->SetBranchAddress("file_offset", &file_offset);

    int nEntries = clusterTree->GetEntries();
    LogInfo << "Number of entries: " << nEntries << std::endl;

    // Create histograms for analysis (similar to dataAnalyzer)
    TH1I *h_clustersPerEvent[4];
    for (int d = 0; d < 4; ++d) {
        h_clustersPerEvent[d] = new TH1I(Form("h_clustersPerEvent_D%d", d),
                                         Form("Clusters per event - D%d;Clusters;Events", d),
                                         11, -0.5, 10.5);
    }

    TH1I *h_totalClustersPerEvent = new TH1I("h_totalClustersPerEvent",
                                             "Total clusters per event;Clusters;Events",
                                             21, -0.5, 20.5);

    TH1I *h_clusterSizes = new TH1I("h_clusterSizes",
                                    "Cluster sizes;Size (channels);Count",
                                    20, 0.5, 20.5);

    TH1F *h_clusterAmplitudes = new TH1F("h_clusterAmplitudes",
                                         "Cluster amplitudes;Amplitude;Count",
                                         100, 0, 100);

    TH2I *h_clusterSizeVsDetector = new TH2I("h_clusterSizeVsDetector",
                                             "Cluster size vs detector;Detector;Size",
                                             4, -0.5, 3.5, 20, 0.5, 20.5);

    // Histograms for firing channels (similar to dataAnalyzer)
    TH1F *h_firingChannels[4];
    for (int d = 0; d < 4; ++d) {
        h_firingChannels[d] = new TH1F(Form("h_firingChannels_D%d", d),
                                       Form("Firing channels (Detector %d);Channel;Counts", d),
                                       nChannels, 0, nChannels);
    }

    // Histograms for amplitude
    TH1F *h_amplitude[4];
    for (int d = 0; d < 4; ++d) {
        h_amplitude[d] = new TH1F(Form("h_amplitude_D%d", d),
                                  Form("Amplitude (Detector %d);Amplitude;Counts", d),
                                  100, -50, 150);
    }

    // 2D histograms for amplitude vs channel
    TH2F *h_amplitudeVsChannel[4];
    for (int d = 0; d < 4; ++d) {
        h_amplitudeVsChannel[d] = new TH2F(Form("h_amplitudeVsChannel_D%d", d),
                                           Form("Amplitude vs Channel (Detector %d);Channel;Amplitude", d),
                                           nChannels, 0, nChannels, 100, -50, 150);
    }

    // Hits per event (across all detectors) similar to dataAnalyzer
    TH1F *h_hitsInEvent = new TH1F("h_hitsInEvent", "Hits in event;Hits;Counts", 21, -0.5, 20.5);

    // Raw peak histograms
    TH1F *h_rawPeak[4][nChannels];
    for (int d = 0; d < 4; ++d) {
        for (int ch = 0; ch < nChannels; ++ch) {
            h_rawPeak[d][ch] = new TH1F(Form("h_rawPeak_D%d_Ch%d", d, ch),
                                        Form("Raw Peak D%d Ch%d;ADC;Counts", d, ch),
                                        200, 0, 200);
        }
    }

    // 2D Tracking histograms (similar to dataAnalyzer)
    TH2F *h_clusterPositions = new TH2F("h_clusterPositions",
                                        "Cluster positions;Detector;Channel",
                                        4, -0.5, 3.5, nChannels, 0, nChannels);

    TH2F *h_clusterCenters = new TH2F("h_clusterCenters",
                                      "Cluster center positions;Detector;Center Channel",
                                      4, -0.5, 3.5, nChannels, 0, nChannels);

    TH2F *h_clustersPerEvent2D = new TH2F("h_clustersPerEvent2D",
                                          "Clusters per event per detector;Detector;Clusters",
                                          4, -0.5, 3.5, 11, -0.5, 10.5);

    // Timestamp graphs and deltaT histogram (match dataAnalyzer style)
    TGraph *g_evtVsTime = new TGraph(); g_evtVsTime->SetName("g_evtVsTime"); g_evtVsTime->SetTitle("Event index vs internal time;Event index;Time since start (s)");
    TGraph *g_extEvtVsTime = new TGraph(); g_extEvtVsTime->SetName("g_extEvtVsTime"); g_extEvtVsTime->SetTitle("Event index vs external time;Event index;Ext time since start (s)");
    TGraph *g_extVsIntTime = new TGraph(); g_extVsIntTime->SetName("g_extVsIntTime"); g_extVsIntTime->SetTitle("External vs internal time;Internal time since start (s);External time since start (s)");
    TH1F *h_evtDeltaT = new TH1F("h_evtDeltaT", "#Delta t between consecutive events;#Delta t (s);Counts", 100, 0, 1.0);

    // Process events
    int totalEventsWithClusters = 0;
    int totalClusters = 0;
    int eventsWithHits = 0;

    // For timestamps normalization and delta computation
    bool firstTimeSeen = false;
    double t0_int = 0.0, t0_ext = 0.0, prev_t_int = 0.0;

    for (int entry = 0; entry < nEntries; ++entry) {
        clusterTree->GetEntry(entry);
        eventInfoTree->GetEntry(entry);

        // Read detector data
        for (int d = 0; d < nDetectors; ++d) {
            detectorTrees[d]->GetEntry(entry);
            rawDetectorTrees[d]->GetEntry(entry);
        }

        if (verbose && entry % 1000 == 0) LogInfo << "Processing event " << entry << std::endl;

        // Count clusters per detector for this event
        int clustersPerDetector[4] = {0, 0, 0, 0};
        int totalClustersThisEvent = cluster_detectors->size();

        if (totalClustersThisEvent > 0) {
            totalEventsWithClusters++;
        }

        totalClusters += totalClustersThisEvent;

        // Fill per-detector histograms
        for (size_t i = 0; i < cluster_detectors->size(); ++i) {
            int det = (*cluster_detectors)[i];
            if (det >= 0 && det < 4) {
                clustersPerDetector[det]++;
                h_clusterSizes->Fill((*cluster_sizes)[i]);
                h_clusterAmplitudes->Fill((*cluster_amplitudes)[i]);
                h_clusterSizeVsDetector->Fill(det, (*cluster_sizes)[i]);

                // Fill 2D tracking histograms
                int start_ch = (*cluster_start_ch)[i];
                int end_ch = (*cluster_end_ch)[i];
                int center_ch = (start_ch + end_ch) / 2;

                // Fill cluster positions (all channels in cluster)
                for (int ch = start_ch; ch <= end_ch && ch < nChannels; ++ch) {
                    h_clusterPositions->Fill(det, ch);
                }

                // Fill cluster centers
                h_clusterCenters->Fill(det, center_ch);
            }
        }

        // Fill histograms
        for (int d = 0; d < 4; ++d) {
            h_clustersPerEvent[d]->Fill(clustersPerDetector[d]);
            h_clustersPerEvent2D->Fill(d, clustersPerDetector[d]);
        }
        h_totalClustersPerEvent->Fill(totalClustersThisEvent);

    // Fill firing channels and amplitude histograms (similar to dataAnalyzer)
    bool hasHits = false;
    int hitsThisEvent = 0;
        for (int d = 0; d < nDetectors; ++d) {
            for (int ch = 0; ch < nChannels && ch < (int)detectorData[d]->size(); ++ch) {
                float value = (*detectorData[d])[ch];
                float sigma = (*sigmaData[d])[ch];
                float raw_value = (*rawDetectorData[d])[ch];

                // Fill raw peak histograms
                if (ch < nChannels) {
                    h_rawPeak[d][ch]->Fill(raw_value);
                }

                // Check if this is a hit (above threshold)
                double thrNSigma = (nSigma > 0 ? (double)nSigma : 5.0);
                if (!ChannelMask(ch) && value > thrNSigma * sigma) {  // Using configurable sigma threshold
                    hasHits = true;
                    hitsThisEvent++;
                    h_firingChannels[d]->Fill(ch);
                    h_amplitude[d]->Fill(value);
                    h_amplitudeVsChannel[d]->Fill(ch, value);
                }
            }
        }
        if (hasHits) eventsWithHits++;
        h_hitsInEvent->Fill(hitsThisEvent);

        // Timestamps handling
        // Convert timestamps to seconds assuming 1 tick = 1 microsecond if units are unknown
        // If units are already seconds, scaling factors of 1 won't hurt visually
        double t_int = (double)timestamp * 1e-6;
        double t_ext = (double)ext_timestamp * 1e-6;
        if (!firstTimeSeen) {
            firstTimeSeen = true; t0_int = t_int; t0_ext = t_ext; prev_t_int = t_int;
        }
        double rel_int = t_int - t0_int;
        double rel_ext = t_ext - t0_ext;
        g_evtVsTime->SetPoint(g_evtVsTime->GetN(), entry, rel_int);
        g_extEvtVsTime->SetPoint(g_extEvtVsTime->GetN(), entry, rel_ext);
        g_extVsIntTime->SetPoint(g_extVsIntTime->GetN(), rel_int, rel_ext);
        double dt = t_int - prev_t_int; prev_t_int = t_int;
        if (dt >= 0) h_evtDeltaT->Fill(dt);
    }

    LogInfo << "Analysis complete:" << std::endl;
    LogInfo << "Total events: " << nEntries << std::endl;
    LogInfo << "Events with clusters: " << totalEventsWithClusters << std::endl;
    LogInfo << "Events with hits: " << eventsWithHits << std::endl;
    LogInfo << "Total clusters: " << totalClusters << std::endl;

    // Create PDF report (similar to dataAnalyzer)
    std::string input_file_base = GetBaseName(inputFile);
    std::string output_filename_report;
    if (nSigma > 0) {
        output_filename_report = outputDir + "/" + input_file_base + "_" + std::to_string(nSigma) + "sigma_report.pdf";
    } else {
        output_filename_report = outputDir + "/" + input_file_base + "_report.pdf";
    }

    LogInfo << "Output PDF report: " << output_filename_report << std::endl;

    // Check output directory permissions
    if (system(("test -w " + outputDir).c_str()) != 0) {
        LogError << "Output directory " << outputDir << " is not writable!" << std::endl;
        return 1;
    }

    // Ensure output directory exists
    system(("mkdir -p " + outputDir).c_str());

    // Root app
    TApplication *app = new TApplication("app", &argc, argv);

    // Set root style
    gStyle->SetOptStat("emruo");
    gStyle->SetTitleFontSize(0.045);

    // Extract run number from filename
    std::string runNumber = "unknown";
    size_t pos = input_file_base.find("RUN");
    if (pos != std::string::npos) {
        size_t endPos = input_file_base.find("_", pos);
        if (endPos != std::string::npos) {
            runNumber = input_file_base.substr(pos + 3, endPos - pos - 3);
        }
    }

    // Define detector colors and markers (similar to dataAnalyzer)
    Color_t detColors[4] = {kRed, kBlue, kGreen+2, kMagenta};
    int detMarkers[4] = {20, 21, 22, 23};

    try {
        LogInfo << "Creating multi-page PDF report..." << std::endl;
        auto drawPageTag = [](int page){ TLatex pageNum; pageNum.SetNDC(true); pageNum.SetTextSize(0.025); pageNum.SetTextAlign(31); pageNum.DrawLatex(0.95, 0.03, Form("Page %d", page)); };
        int page = 1;
        // Page 1: Summary page
        TCanvas *c_summary = new TCanvas(Form("c_summary_Run%s", runNumber.c_str()),
                                         Form("Run %s Summary", runNumber.c_str()), 800, 600);
        c_summary->cd();
        TLatex lat;
        lat.SetNDC(true);
        lat.SetTextSize(0.030);
        double yTop = 0.93;
        const double dyHead = 0.05;

        // Add page number
        lat.SetTextSize(0.025);
        lat.SetTextAlign(31);
        lat.DrawLatex(0.95, 0.03, "Page 1");
        lat.SetTextAlign(11);
        lat.SetTextSize(0.030);

        lat.DrawLatex(0.10, yTop, Form("Run %s cluster summary", runNumber.c_str()));
        yTop -= dyHead;

        // Statistics table
        const double rowH = 0.035;
        double xL = 0.04, xL_num = xL + 0.35, xL_pct = xL + 0.50;
        double yL = yTop - 0.03;

        lat.DrawLatex(xL_num, yL, "number");
        lat.DrawLatex(xL_pct, yL, "percentage");
        yL -= rowH;

        lat.DrawLatex(xL, yL, "Total events");
        lat.DrawLatex(xL_num, yL, Form("%d", nEntries));
        lat.DrawLatex(xL_pct, yL, "100.00%");
        yL -= rowH;

        lat.DrawLatex(xL, yL, "Events with clusters");
        lat.DrawLatex(xL_num, yL, Form("%d", totalEventsWithClusters));
        lat.DrawLatex(xL_pct, yL, Form("%.2f%%", (nEntries > 0) ? (100.0 * totalEventsWithClusters / nEntries) : 0.0));
        yL -= rowH;

        lat.DrawLatex(xL, yL, "Events with hits");
        lat.DrawLatex(xL_num, yL, Form("%d", eventsWithHits));
        lat.DrawLatex(xL_pct, yL, Form("%.2f%%", (nEntries > 0) ? (100.0 * eventsWithHits / nEntries) : 0.0));
        yL -= rowH;

        lat.DrawLatex(xL, yL, "Total clusters");
        lat.DrawLatex(xL_num, yL, Form("%d", totalClusters));
        lat.DrawLatex(xL_pct, yL, Form("%.2f%%", (nEntries > 0) ? (100.0 * totalClusters / nEntries) : 0.0));
        yL -= rowH;

        // Average clusters per event
        double avgClustersPerEvent = (nEntries > 0) ? (double)totalClusters / nEntries : 0.0;
        lat.DrawLatex(xL, yL, "Avg clusters/event");
        lat.DrawLatex(xL_num, yL, Form("%.2f", avgClustersPerEvent));
        yL -= rowH;

        c_summary->Update();
        c_summary->SaveAs((output_filename_report + "(").c_str());

    // Page 2: Clusters per event per detector
        TCanvas *c_clustersPerEvent = new TCanvas(Form("c_clustersPerEvent_Run%s", runNumber.c_str()),
                                                  Form("Clusters per event (Run %s)", runNumber.c_str()), 800, 600);
        c_clustersPerEvent->Divide(2, 2);
    c_clustersPerEvent->cd();
    drawPageTag(++page);

        for (int d = 0; d < 4; ++d) {
            c_clustersPerEvent->cd(d + 1);
            gPad->SetLogy(0);
            h_clustersPerEvent[d]->SetTitle(Form("Clusters per event - D%d (Run %s);Clusters;Events", d, runNumber.c_str()));
            h_clustersPerEvent[d]->GetXaxis()->SetRangeUser(0, 6);
            double maxVal = h_clustersPerEvent[d]->GetMaximum();
            if (maxVal > 0) h_clustersPerEvent[d]->SetMaximum(maxVal * 1.25);
            h_clustersPerEvent[d]->Draw();

            // Add percentages
            double totEv = (double)nEntries;
            int nb = h_clustersPerEvent[d]->GetNbinsX();
            TLatex lab;
            lab.SetTextSize(0.035);
            lab.SetTextAlign(21);
            for (int b = 1; b <= nb; ++b) {
                double val = h_clustersPerEvent[d]->GetBinContent(b);
                if (val <= 0) continue;
                double x = h_clustersPerEvent[d]->GetBinCenter(b);
                double y = val * 1.02;
                double pct = (totEv > 0.0) ? (100.0 * val / totEv) : 0.0;
                lab.DrawLatex(x, y, Form("%.1f%%", pct));
            }
        }
        c_clustersPerEvent->Update();
        c_clustersPerEvent->SaveAs(output_filename_report.c_str());

    // Page 3: Total clusters per event
        TCanvas *c_totalClusters = new TCanvas(Form("c_totalClusters_Run%s", runNumber.c_str()),
                                               Form("Total clusters per event (Run %s)", runNumber.c_str()), 800, 600);
        c_totalClusters->cd();
        gPad->SetLogy(0);
        h_totalClustersPerEvent->SetTitle(Form("Total clusters per event (Run %s);Clusters;Events", runNumber.c_str()));
        h_totalClustersPerEvent->Draw();
    drawPageTag(++page);

        c_totalClusters->Update();
        c_totalClusters->SaveAs(output_filename_report.c_str());

    // Page 4: Cluster sizes
        TCanvas *c_clusterSizes = new TCanvas(Form("c_clusterSizes_Run%s", runNumber.c_str()),
                                              Form("Cluster sizes (Run %s)", runNumber.c_str()), 800, 600);
        c_clusterSizes->cd();
        gPad->SetLogy(1); // Log scale for sizes
        h_clusterSizes->SetTitle(Form("Cluster sizes (Run %s);Size (channels);Count", runNumber.c_str()));
        h_clusterSizes->Draw();
    drawPageTag(++page);

        c_clusterSizes->Update();
        c_clusterSizes->SaveAs(output_filename_report.c_str());

    // Page 5: Cluster amplitudes
        TCanvas *c_clusterAmplitudes = new TCanvas(Form("c_clusterAmplitudes_Run%s", runNumber.c_str()),
                                                   Form("Cluster amplitudes (Run %s)", runNumber.c_str()), 800, 600);
        c_clusterAmplitudes->cd();
        gPad->SetLogy(1); // Log scale for amplitudes
        h_clusterAmplitudes->SetTitle(Form("Cluster amplitudes (Run %s);Amplitude;Count", runNumber.c_str()));
        h_clusterAmplitudes->Draw();
    drawPageTag(++page);

        c_clusterAmplitudes->Update();
        c_clusterAmplitudes->SaveAs(output_filename_report.c_str());

    // Page 6: Cluster size vs detector
        TCanvas *c_sizeVsDetector = new TCanvas(Form("c_sizeVsDetector_Run%s", runNumber.c_str()),
                                                Form("Cluster size vs detector (Run %s)", runNumber.c_str()), 800, 600);
        c_sizeVsDetector->cd();
        h_clusterSizeVsDetector->SetTitle(Form("Cluster size vs detector (Run %s);Detector;Size", runNumber.c_str()));
        h_clusterSizeVsDetector->Draw("COLZ");
    drawPageTag(++page);

        c_sizeVsDetector->Update();
        c_sizeVsDetector->SaveAs(output_filename_report.c_str());

    // Page 7: Firing channels (similar to dataAnalyzer)
        TCanvas *c_channelsFiring = new TCanvas(Form("c_channelsFiring_Run%s", runNumber.c_str()),
                                                Form("Channels Firing (Run %s)", runNumber.c_str()), 800, 600);
        c_channelsFiring->cd();
        gPad->SetLogy(1);
        h_firingChannels[0]->SetTitle(Form("Firing channels (Run %s);Channel;Counts", runNumber.c_str()));
        h_firingChannels[0]->SetLineColor(detColors[0]);
        h_firingChannels[0]->Draw();

        TLegend *leg = new TLegend(0.7, 0.7, 0.9, 0.9);
        leg->AddEntry(h_firingChannels[0], "D0", "l");

        for (int d = 1; d < 4; ++d) {
            h_firingChannels[d]->SetLineColor(detColors[d]);
            h_firingChannels[d]->Draw("same");
            leg->AddEntry(h_firingChannels[d], Form("D%d", d), "l");
        }
        leg->Draw();

    drawPageTag(++page);

        c_channelsFiring->Update();
    c_channelsFiring->SaveAs(output_filename_report.c_str());

    // Page 8: Amplitude distributions
        TCanvas *c_amplitude = new TCanvas(Form("c_amplitude_Run%s", runNumber.c_str()),
                                           Form("Amplitude (Run %s)", runNumber.c_str()), 800, 600);
        c_amplitude->cd();
        gPad->SetLogy(1);
        h_amplitude[0]->SetTitle(Form("Amplitude (Run %s);Amplitude;Counts", runNumber.c_str()));
        h_amplitude[0]->SetLineColor(detColors[0]);
        h_amplitude[0]->Draw();

        TLegend *leg_amp = new TLegend(0.7, 0.7, 0.9, 0.9);
        leg_amp->AddEntry(h_amplitude[0], "D0", "l");

        for (int d = 1; d < 4; ++d) {
            h_amplitude[d]->SetLineColor(detColors[d]);
            h_amplitude[d]->Draw("same");
            leg_amp->AddEntry(h_amplitude[d], Form("D%d", d), "l");
        }
        leg_amp->Draw();

        drawPageTag(++page);

        c_amplitude->Update();
    c_amplitude->SaveAs(output_filename_report.c_str());

        // Page 9: Amplitude vs Channel heatmaps (2x2)
        {
            TCanvas *c_ampVsCh = new TCanvas(Form("c_ampVsChannel_Run%s", runNumber.c_str()),
                                             Form("Amplitude vs Channel (Run %s)", runNumber.c_str()), 1200, 900);
            c_ampVsCh->Divide(2,2);
            for (int d=0; d<4; ++d) {
                c_ampVsCh->cd(d+1);
                h_amplitudeVsChannel[d]->Draw("COLZ");
            }
            c_ampVsCh->cd();
            drawPageTag(++page);
            c_ampVsCh->Update();
            c_ampVsCh->SaveAs(output_filename_report.c_str());
        }

    // Page 10: 2D Cluster Positions
        TCanvas *c_clusterPositions = new TCanvas(Form("c_clusterPositions_Run%s", runNumber.c_str()),
                                                  Form("Cluster Positions (Run %s)", runNumber.c_str()), 800, 600);
        c_clusterPositions->cd();
        h_clusterPositions->SetTitle(Form("Cluster positions (Run %s);Detector;Channel", runNumber.c_str()));
        h_clusterPositions->Draw("COLZ");
    drawPageTag(++page);

        c_clusterPositions->Update();
    c_clusterPositions->SaveAs(output_filename_report.c_str());

    // Page 11: Cluster Centers
        TCanvas *c_clusterCenters = new TCanvas(Form("c_clusterCenters_Run%s", runNumber.c_str()),
                                                Form("Cluster Centers (Run %s)", runNumber.c_str()), 800, 600);
        c_clusterCenters->cd();
        h_clusterCenters->SetTitle(Form("Cluster center positions (Run %s);Detector;Center Channel", runNumber.c_str()));
        h_clusterCenters->Draw("COLZ");
    drawPageTag(++page);

        c_clusterCenters->Update();
    c_clusterCenters->SaveAs(output_filename_report.c_str());

        // Page 12: Clusters per Event 2D
        TCanvas *c_clustersPerEvent2D = new TCanvas(Form("c_clustersPerEvent2D_Run%s", runNumber.c_str()),
                                                    Form("Clusters per Event per Detector (Run %s)", runNumber.c_str()), 800, 600);
        c_clustersPerEvent2D->cd();
        h_clustersPerEvent2D->SetTitle(Form("Clusters per event per detector (Run %s);Detector;Clusters", runNumber.c_str()));
        h_clustersPerEvent2D->Draw("COLZ");
        drawPageTag(++page);

        c_clustersPerEvent2D->Update();
        c_clustersPerEvent2D->SaveAs(output_filename_report.c_str());

        // Page 13: Hits per event (all detectors)
        {
            TCanvas *c_hits = new TCanvas(Form("c_hitsInEvent_Run%s", runNumber.c_str()), Form("Hits per event - Run %s", runNumber.c_str()), 800, 600);
            c_hits->cd();
            gPad->SetLogy(0);
            h_hitsInEvent->SetTitle(Form("Hits per event (Run %s);Hits;Events", runNumber.c_str()));
            h_hitsInEvent->Draw();
            drawPageTag(++page);
            c_hits->Update();
            c_hits->SaveAs(output_filename_report.c_str());
        }

        // Page 14: Timestamps 2x2 (evt vs time, ext vs time, ext vs int, deltaT)
        {
            TCanvas *c_time2x2 = new TCanvas(Form("c_time2x2_Run%s", runNumber.c_str()), Form("Timestamps - Run %s", runNumber.c_str()), 1200, 900);
            c_time2x2->Divide(2,2);
            c_time2x2->cd();
            drawPageTag(++page);
            int padIdx = 1;
            if (g_evtVsTime && g_evtVsTime->GetN()>0) { c_time2x2->cd(padIdx++); g_evtVsTime->Draw("AP"); }
            if (g_extEvtVsTime && g_extEvtVsTime->GetN()>0) { c_time2x2->cd(padIdx++); g_extEvtVsTime->Draw("AP"); }
            if (g_extVsIntTime && g_extVsIntTime->GetN()>0) { c_time2x2->cd(padIdx++); g_extVsIntTime->Draw("AP"); }
            if (h_evtDeltaT) {
                c_time2x2->cd(4);
                h_evtDeltaT->Draw();
                int firstNonZeroBin = 1; while (firstNonZeroBin <= h_evtDeltaT->GetNbinsX() && h_evtDeltaT->GetBinContent(firstNonZeroBin) <= 0) ++firstNonZeroBin;
                double fitMin = h_evtDeltaT->GetXaxis()->GetBinLowEdge(std::max(1, firstNonZeroBin));
                double fitMax = h_evtDeltaT->GetXaxis()->GetXmax();
                if (fitMax > fitMin) {
                    TF1 *fexpo = new TF1("f_evtDelta_expo", "expo", fitMin, fitMax);
                    fexpo->SetLineColor(kRed+1);
                    h_evtDeltaT->Fit(fexpo, "RQ");
                    double slope = fexpo->GetParameter(1);
                    double slopeErr = fexpo->GetParError(1);
                    double tau = (slope < 0) ? -1.0/slope : 0.0;
                    double tauErr = 0.0;
                    if (slope < 0 && slopeErr > 0) tauErr = (1.0/(slope*slope)) * slopeErr;
                    TLatex lfit; lfit.SetNDC(true); lfit.SetTextSize(0.03);
                    lfit.SetTextColor(kRed+2);
                    lfit.SetTextAlign(22);
                    lfit.DrawLatex(0.50, 0.28, Form("Exp fit: #tau = %.3g #pm %.2g s", tau, tauErr));
                }
            }
            c_time2x2->Update();
            c_time2x2->SaveAs(output_filename_report.c_str());
        }

    // Page 15: Baseline and Sigma vs channel
        TCanvas *c_baseline_sigma = new TCanvas(Form("c_baseline_sigma_Run%s", runNumber.c_str()),
                                                Form("Baseline and Sigma - Run %s", runNumber.c_str()), 900, 800);
        c_baseline_sigma->Divide(1, 2);

        c_baseline_sigma->cd();
    drawPageTag(++page);

        // Top pad: Baseline vs channel
        c_baseline_sigma->cd(1);
        double yMin = 1e300, yMax = -1e300;
        for (int d = 0; d < 4; ++d) {
            for (int ch = 0; ch < nChannels; ++ch) {
                double v = (*pedestalData[d])[ch];
                if (v < yMin) yMin = v;
                if (v > yMax) yMax = v;
            }
        }
        if (yMax > yMin) {
            TH1F *frame_base = new TH1F("frame_base", Form("Baseline vs channel - Run %s;Channel;Baseline", runNumber.c_str()), nChannels, 0, nChannels);
            frame_base->SetStats(0);
            double pad = 0.05 * (yMax - yMin + 1e-6);
            frame_base->GetYaxis()->SetRangeUser(yMin - pad, yMax + pad);
            frame_base->Draw();

            TLegend *leg_base = new TLegend(0.75, 0.15, 0.95, 0.35);
            leg_base->SetBorderSize(0);
            leg_base->SetFillStyle(0);
            leg_base->SetTextSize(0.03);

            for (int d = 0; d < 4; ++d) {
                TGraph *gr = new TGraph(nChannels);
                gr->SetTitle(Form("D%d", d));
                for (int ch = 0; ch < nChannels; ++ch) {
                    gr->SetPoint(ch, ch, (*pedestalData[d])[ch]);
                }
                gr->SetMarkerStyle(detMarkers[d]);
                gr->SetMarkerSize(0.6);
                gr->SetMarkerColor(detColors[d]);
                gr->SetLineColor(detColors[d]);
                gr->Draw("P SAME");
                leg_base->AddEntry(gr, Form("D%d", d), "lp");
            }
            leg_base->Draw();
        }

        // Bottom pad: Sigma vs channel
        c_baseline_sigma->cd(2);
        yMin = 1e300, yMax = -1e300;
        for (int d = 0; d < 4; ++d) {
            for (int ch = 0; ch < nChannels; ++ch) {
                double v = (*sigmaData[d])[ch];
                if (v < yMin) yMin = v;
                if (v > yMax) yMax = v;
            }
        }
        if (yMax > yMin) {
            TH1F *frame_sig = new TH1F("frame_sig", Form("Sigma vs channel - Run %s;Channel;Sigma", runNumber.c_str()), nChannels, 0, nChannels);
            frame_sig->SetStats(0);
            double pad = 0.05 * (yMax - yMin + 1e-6);
            frame_sig->GetYaxis()->SetRangeUser(yMin - pad, yMax + pad);
            frame_sig->Draw();

            TLegend *leg_sig = new TLegend(0.75, 0.15, 0.95, 0.35);
            leg_sig->SetBorderSize(0);
            leg_sig->SetFillStyle(0);
            leg_sig->SetTextSize(0.03);

            for (int d = 0; d < 4; ++d) {
                TGraph *gr = new TGraph(nChannels);
                for (int ch = 0; ch < nChannels; ++ch) {
                    gr->SetPoint(ch, ch, (*sigmaData[d])[ch]);
                }
                gr->SetMarkerStyle(detMarkers[d]);
                gr->SetMarkerSize(0.6);
                gr->SetMarkerColor(detColors[d]);
                gr->SetLineColor(detColors[d]);
                gr->Draw("P SAME");
                leg_sig->AddEntry(gr, Form("D%d", d), "lp");
            }
            leg_sig->Draw();
        }

        c_baseline_sigma->Update();
        c_baseline_sigma->SaveAs((output_filename_report + ")").c_str());

        LogInfo << "Multi-page PDF report saved successfully: " << output_filename_report << std::endl;

    } catch (const std::exception& e) {
        LogError << "Error saving PDF report: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        LogError << "Unknown error occurred while saving PDF report" << std::endl;
        return 1;
    }

    inputRootFile->Close();

    LogInfo << "Report generation completed successfully." << std::endl;

    return 0;
}
