///////////////////////////////////////
// Created by E. Villa on 2024-08-27.//
// Code to look at the data of the   //
// NP02 beam monitor.                //
///////////////////////////////////////

// #include <iostream>
// #include <string>
// #include <vector>
// #include <fstream>
// #include <sstream>

#include "TFile.h"
#include "TTree.h"
#include "TBranch.h"
#include "TH1F.h"
#include "TH1I.h"
#include "TH2F.h"
#include "TCanvas.h"
#include "TGraph.h"
#include "TApplication.h"
#include "TStyle.h"
#include "TSystem.h"
#include "TMarker.h"
#include "TLatex.h"
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <map>
#include <array>

#include <nlohmann/json.hpp>

#include "CmdLineParser.h"
#include "Logger.h"
#include "ocaEvent.h"

LoggerInit([]{
  Logger::getUserHeader() << "[" << FILENAME << "]";
});

int main(int argc, char* argv[]) {

    CmdLineParser clp;

    clp.getDescription() << "> This program takes a root file and a calibration file and analyzes it." << std::endl;

    clp.addDummyOption("Main options");
    clp.addOption("runNumber", {"-n", "--run-number"}, "Specify run number.");
    clp.addOption("appSettings",    {"-j", "--json-settings"},   "Specify application settings file path.");
    clp.addOption("inputRootFile",  {"-r", "--root-file"},      "Root converted data");
    clp.addOption("inputCalFile",   {"-c", "--cal-file"},       "Calibration file.");
    clp.addOption("outputDir",      {"-o", "--output"},         "Specify output directory path");
    clp.addOption("nSigma",         {"-s", "--n-sigma"},        "Number of sigmas above pedestal to consider signal");

    clp.addDummyOption("Triggers");
    clp.addTriggerOption("verboseMode",     {"-v"},             "RunVerboseMode, bool");
    clp.addTriggerOption("debugMode",       {"-d"},             "RunDebugMode, bool");

    clp.addDummyOption();

    // usage always displayed
    LogInfo << clp.getDescription().str() << std::endl;

    LogInfo << "Usage: " << std::endl;
    LogInfo << clp.getConfigSummary() << std::endl << std::endl;

    clp.parseCmdLine(argc, argv);

    LogThrowIf( clp.isNoOptionTriggered(), "No option was provided." );

    LogInfo << "Provided arguments: " << std::endl;
    LogInfo << clp.getValueSummary() << std::endl << std::endl;

    bool verbose = clp.isOptionTriggered("verboseMode");
    bool debug = clp.isOptionTriggered("debugMode");
    
    // read json settings
    std::string jsonSettingsFile = "";
    std::ifstream i(jsonSettingsFile);
    nlohmann::json jsonSettings;
    if (clp.isOptionTriggered("appSettings")) {
        jsonSettingsFile = clp.getOptionVal<std::string>("appSettings");
        LogInfo << "Reading JSON settings from: " << jsonSettingsFile << std::endl;
        i.open(jsonSettingsFile);
        if (i.is_open()) {
            i >> jsonSettings;
            LogInfo << "JSON settings loaded successfully." << std::endl;
        } else {
            LogError << "Failed to read JSON settings from: " << jsonSettingsFile << std::endl;
            return 1;
        }
    } else {
        LogInfo << "No JSON settings file provided, using default values." << std::endl;
    }

    ///////////////////////////
    // Some variables

    int nDetectors = 4;
    int nChannels = 384;

    ///////////////////////////

    // get calibration file
    std::string inputCalFile = clp.getOptionVal<std::string>("inputCalFile");
    LogInfo << "Calibration file: " << inputCalFile << std::endl;
    std::ifstream calFile(inputCalFile);
    // check if the file is open correctly
    if (!calFile.is_open()) {
        std::cout << "Error: calibration file not open" << std::endl;
        return 1;
    }

    // the format is // TODO move to some docs
    // 0: channel
    // 1: channel / 64      // I think this is the chip
    // 2: va_chan           // I think this is the channel on the chuip
    // 3: pedestals->at(ch)
    // 4: rsigma->at(ch)
    // 5: sigma_value
    // 6: badchan
    // 7: 0.000

    /// Calib file
    // read the calibration file and store the values in a vector
    std::vector <std::vector <float>> baseline (nDetectors, std::vector<float>(nChannels));
    
    std::vector<std::vector<float>> baseline_sigma(nDetectors, std::vector<float>(nChannels));

    // skip header lines, starting with #

    LogInfo << "Reading calibration file..." << std::endl;
    std::string line;
    for (int detit = 0; detit < nDetectors ; detit++){
        // skip the first 18 lines, header
        for (int i = 0; i < 18; i++) std::getline(calFile, line); // skipping, header

        for (int i = 0; i < nChannels; i++) {
            std::getline(calFile, line);
            // values are separated by commas, read all and store line by line
            std::istringstream iss(line);
            std::vector <float> values;
            float value;
            std::string token;
            while (std::getline(iss, token, ',')) {
                std::istringstream iss_value(token);
                float value;
                if (iss_value >> value) {
                values.push_back(value);
                }
            }

            // check if the values are correct
            if (values.size() != 8) {
                LogError << "Error: wrong number of values in the calibration file" << std::endl;
                LogError << "Values size: " << values.size() << std::endl;
                return 1;
            }

            // store the values
            int this_channel = values.at(0);
            float this_baseline = values.at(3);
            float this_baseline_sigma = values.at(5);

            if (verbose) LogInfo << "Channel: " << this_channel << " Detector: " << detit << " Baseline: " << this_baseline << " Baseline sigma: " << this_baseline_sigma << std::endl;

            baseline.at(detit).emplace(baseline.at(detit).begin() + this_channel, this_baseline);
            baseline_sigma.at(detit).emplace(baseline_sigma.at(detit).begin() + this_channel, this_baseline_sigma);

        }
    }

    LogInfo << "Stored baseline and sigma for all channels locally" << std::endl;

    /// ROOT file

    // Get root file name
    std::string input_root_filename = clp.getOptionVal<std::string>("inputRootFile");
    LogInfo << "Root file: " << input_root_filename << std::endl;   
    TFile *input_root_file = new TFile(input_root_filename.c_str(), "READ");
    // check if the file is open correctly
    if( !input_root_file->IsOpen() ){
        LogError << "Error: file not open" << std::endl;
        return 1;
    }

    // get trees with validation
    const char* treeNames[4]   = {"raw_events", "raw_events_B", "raw_events_C", "raw_events_D"};
    const char* branchNames[4] = {"RAW Event", "RAW Event B", "RAW Event C", "RAW Event D"};
    std::vector<TTree*> raw_events_trees; raw_events_trees.reserve(nDetectors);
    for (int i=0;i<nDetectors;i++) {
        TTree *t = (TTree*) input_root_file->Get(treeNames[i]);
        if (!t) {
            LogError << "Missing required TTree '" << treeNames[i] << "' in input ROOT file. Aborting to avoid crash." << std::endl;
            return 1;
        }
        if (!t->GetBranch(branchNames[i])) {
            LogError << "Missing required branch '" << branchNames[i] << "' in tree '" << treeNames[i] << "'. Aborting to avoid crash." << std::endl;
            return 1;
        }
        raw_events_trees.emplace_back(t);
    }

    LogInfo << "Got the trees" << std::endl;

    // get the number of entries
    std::vector <int> nEntries = std::vector <int>();
    nEntries.reserve(nDetectors);
    for (int detit = 0; detit < nDetectors; detit++) {
        nEntries.emplace_back(raw_events_trees.at(detit)->GetEntries());
        LogInfo << "Detector " << detit << " has " << nEntries.at(detit) << " entries" << std::endl;
    }

    // Entries should always be the same for all detectors
    // if (nEntries.at(0) != nEntries.at(1) || nEntries.at(0) != nEntries.at(2) || nEntries.at(0) != nEntries.at(3)) {
    //     LogError << "Error: number of entries is different for the detectors! Something went wrong" << std::endl;
    //     return 1;
    // }

    ///////////////////////////
    
    /// Create some objects to plot results

    // Root app
    TApplication *app = new TApplication("app", &argc, argv);

    // set root to displat overflow and underflow in stat box
    gStyle->SetOptStat("emruo"); // e: entries, m:
    // Slightly reduce title font size globally to help long titles fit
    gStyle->SetTitleFontSize(0.045);

    // Create a vector of TF1 objects to show the channels that fire, one for each detector
    std::vector <TH1F*> *h_firingChannels = new std::vector <TH1F*>;
    h_firingChannels->reserve(nDetectors);
    for (int i = 0; i < nDetectors; i++) {
        TH1F *this_h_firingChannels = new TH1F(Form("Firing channels (Detector %d)", i), Form("Firing channels (Detector %d)", i), nChannels, 0, nChannels);
        this_h_firingChannels->GetXaxis()->SetTitle("Channel");
        this_h_firingChannels->GetYaxis()->SetTitle("Counts");
        h_firingChannels->emplace_back(this_h_firingChannels);
    }

    // plot values of sigma per channel in a tgraph
    std::vector <TGraph*> *g_sigma = new std::vector <TGraph*>;
    g_sigma->reserve(nDetectors);
    for (int i = 0; i < nDetectors; i++) {
        TGraph *this_g_sigma = new TGraph(nChannels);
        this_g_sigma->SetTitle(Form("Sigma (Detector %d)", i));
        this_g_sigma->GetXaxis()->SetTitle("Channel");
        this_g_sigma->GetYaxis()->SetTitle("Sigma");
        this_g_sigma->SetMarkerStyle(20);
        this_g_sigma->SetMarkerSize(0.8);
        g_sigma->emplace_back(this_g_sigma);
    }

    // plot baseline
    std::vector <TGraph*> *g_baseline = new std::vector <TGraph*>;
    g_baseline->reserve(nDetectors);
    for (int i = 0; i < nDetectors; i++) {
        TGraph *this_g_baseline = new TGraph(nChannels);
        this_g_baseline->SetTitle(Form("Baseline (Detector %d)", i));
        this_g_baseline->GetXaxis()->SetTitle("Channel");
        this_g_baseline->GetYaxis()->SetTitle("Baseline");
        this_g_baseline->SetMarkerStyle(20);
        this_g_baseline->SetMarkerSize(0.8);
        g_baseline->emplace_back(this_g_baseline);
    }

    // raw peak for each channel
    std::vector <std::vector <TH1F*>*> *h_rawPeak = new std::vector <std::vector <TH1F*>*>;
    h_rawPeak->reserve(nDetectors);
    for (int i = 0; i < nDetectors; i++) {
        LogInfo << "Detector " << i << std::endl;
        std::vector  <TH1F*> *this_h_rawPeak_vector = new std::vector <TH1F*>;
        for (int j = 0; j < nChannels; j++) {
            // LogInfo << "Channel " << j << std::endl;
            TH1F *this_h_rawPeak = new TH1F(Form("Raw peak (Detector %d, Channel %d)", i, j), Form("Raw peak (Detector %d, Channel %d)", i, j), 1000, 0, 2000);
            this_h_rawPeak->GetXaxis()->SetTitle("Peak");
            this_h_rawPeak->GetYaxis()->SetTitle("Counts");
            this_h_rawPeak->GetYaxis()->SetRangeUser(0,5);
            // fill color blue
            this_h_rawPeak->SetFillColor(kBlue);
            this_h_rawPeak_vector->emplace_back(this_h_rawPeak);
        }
        h_rawPeak->emplace_back(this_h_rawPeak_vector);
    }

    std::vector <TH1F*> *h_amplitude = new std::vector <TH1F*>;
    h_amplitude->reserve(nDetectors);
    for (int i = 0; i < nDetectors; i++) {
    // Reduce amplitude range to 0–200 as requested
    TH1F *this_h_amplitude = new TH1F(Form("Amplitude (Detector %d)", i), Form("Amplitude (Detector %d)", i), 100, 0, 200);
        this_h_amplitude->GetXaxis()->SetTitle("Amplitude");
        this_h_amplitude->GetYaxis()->SetTitle("Counts");
        h_amplitude->emplace_back(this_h_amplitude);
    }

    // Add 2D histogram for amplitude vs channel
    std::vector <TH2F*> *h_amplitudeVsChannel = new std::vector <TH2F*>;
    h_amplitudeVsChannel->reserve(nDetectors);
    for (int i = 0; i < nDetectors; i++) {
        TH2F *this_h_amplitudeVsChannel = new TH2F(Form("Amplitude vs Channel (Detector %d)", i), 
                                                   Form("Amplitude vs Channel (Detector %d)", i), 
                                                   nChannels, 0, nChannels, 100, 0, 1000);
        this_h_amplitudeVsChannel->GetXaxis()->SetTitle("Channel");
        this_h_amplitudeVsChannel->GetYaxis()->SetTitle("Amplitude");
        h_amplitudeVsChannel->emplace_back(this_h_amplitudeVsChannel);
    }

    // Extend hits-per-event range to cover up to 20 hits
    TH1F *h_hitsInEvent = new TH1F("Hits in event", "Hits in event", 21, -0.5, 20.5);
    h_hitsInEvent->GetXaxis()->SetTitle("Hits");
    h_hitsInEvent->GetYaxis()->SetTitle("Counts");

    // Histograms: clusters per event per detector (created before event loop)
    TH1I *h_clustersPerEvent[4];
    for (int d = 0; d < 4; ++d) {
        h_clustersPerEvent[d] = new TH1I(Form("h_clustersPerEvent_D%d", d),
                                         Form("Clusters per event - D%d;Clusters;Events", d),
                                         11, -0.5, 10.5);
    }

    // loop over the entries to get the peak. For each entry, store the values in the event
    // TODO temporary, do the real thing

    LogInfo << "Reading the entries and storing in a vector of Events" << std::endl;
    std::vector <Event> * events = new std::vector <Event>();
    events->reserve(nEntries.at(0)); // should all be the same

    LogInfo << "Size of events: " << events->size() << std::endl;
    std::vector<std::vector<float>*>* data = new std::vector<std::vector<float>*>();
    data->reserve(nDetectors);
    for (int i = 0; i < nDetectors; i++) {
        std::vector<float> *this_data = new std::vector<float>;
        this_data->reserve(nChannels);
        data->emplace_back(this_data);
    }

    // Set branch addresses safely
    for (int i=0;i<nDetectors;i++) {
        if (raw_events_trees[i]->SetBranchAddress(branchNames[i], &data->at(i)) < 0) {
            LogError << "Failed to set branch address for '" << branchNames[i] << "' in '" << treeNames[i] << "'." << std::endl;
            return 1;
        }
    }
    
    int limit = nEntries.at(0);
    int maxEvents = 1e9; // default value
    // Overwrite maxEvents if present in json settings file
    if (jsonSettings.contains("maxEvents")) {
        // Check if the value is a number and handle it properly
        if (jsonSettings["maxEvents"].is_number_integer()) {
            maxEvents = jsonSettings["maxEvents"];
            LogInfo << "Overwriting maxEvents from JSON: " << maxEvents << std::endl;
        } else {
            LogError << "maxEvents in JSON is not a valid integer, using default: " << maxEvents << std::endl;
        }
    } else {
        LogInfo << "Using default maxEvents: " << maxEvents << std::endl;
    }
    if (limit > maxEvents) limit = maxEvents;

    // Check if edge channels should be masked
    auto parseBool = [](const nlohmann::json& j, const std::string& key, bool defVal)->bool{
        if (!j.contains(key)) return defVal;
        const auto& v = j.at(key);
        if (v.is_boolean()) return v.get<bool>();
        if (v.is_string()) {
            std::string s = v.get<std::string>();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return (s == "true" || s == "1" || s == "yes" || s == "y");
        }
        if (v.is_number_integer()) return v.get<int>() != 0;
        return defVal;
    };

    bool maskEdgeChannels = parseBool(jsonSettings, "maskEdgeChannels", false);
    LogInfo << "Mask edge channels: " << (maskEdgeChannels ? "true" : "false") << std::endl;

    // Clean sample max cluster width (channels), configurable via JSON
    int cleanMaxWidth = 5;
    try {
        if (jsonSettings.contains("cleanClusterMaxWidth")) {
            const auto &cw = jsonSettings["cleanClusterMaxWidth"];
            if (cw.is_number_integer()) cleanMaxWidth = cw.get<int>();
            else if (cw.is_string()) {
                try { cleanMaxWidth = std::stoi(cw.get<std::string>()); } catch (...) {}
            }
        }
    } catch (...) {}
    if (cleanMaxWidth < 1) cleanMaxWidth = 1;
    LogInfo << "Clean sample max cluster width: " << cleanMaxWidth << " channels" << std::endl;

    // Function to check if a channel is an edge channel (chip boundaries)
    auto isEdgeChannel = [](int channel) -> bool {
        // Edge channels are at chip boundaries: 0, 63, 64, 127, 128, 191, 192, 255, 256, 319, 320, 383
        // Each chip has 64 channels, so edge channels are: 0, 63 of each chip
        int chipNumber = channel / 64;
        int channelInChip = channel % 64;
        return (channelInChip == 0 || channelInChip == 1 || channelInChip == 62 || channelInChip == 63); // it seems also the ones next are noisy
    };

    int hitsInEvent = 0;
    int triggeredEvents = 0; // events passing 2-or-3 cluster criterion (pre width cut)
    int eventsTwoDetOrMore = 0; // events where at least two detectors (D0..D2) have >=1 cluster (any multiplicity per detector)
    long long selected2to3Count = 0; // events used in 2-or-3 display after width cut
    long long selected3Count = 0;    // events used in exactly-3 display after width cut
    int eventsWithHits = 0;   // events with at least one (masked) hit
    
    // Geometry / interpolation setup
    // Strip orientations relative to the global X axis:
    //  - Detector 2 strips run along X (0°)
    //  - Detector 0,1 strips are inclined by -15° and +15° w.r.t. X
    // We project along the normal to strips (orientation +90°) when forming u = x cosθ + y sinθ.
    const int geomDetN = 3;
    const double stripAnglesDeg[geomDetN] = {-15.0, +15.0, 0.0};
    double normalAnglesRad[geomDetN];
    for (int i=0;i<geomDetN;i++) normalAnglesRad[i] = (stripAnglesDeg[i] + 90.0) * M_PI/180.0;
    // Physical pitch: 10 cm across strip-normal over nChannels
    double channelPitch = 100.0 / nChannels; // mm per strip
    
    // Per-detector plane offsets (x0,y0) in mm to express true global coordinates.
    // Preferred source: parameters/geometry.json (or a path provided by settings as geometryParamsPath)
    // Backward compatibility: if file not found, fall back to JSON key planeOffsetsMm.
    std::vector<std::pair<double,double>> planeOffsets(geomDetN, {0.0, 0.0});
    // Remember which geometry file we end up using (for reading other parameters like axis ranges)
    std::string usedGeomPath;
    // Geometry parameters (plane offsets): prefer HOME_DIR/parameters/geometry.json from env,
    // then try a few sensible relative locations depending on the working dir and settings file path
    std::vector<std::string> geomCandidates;
    {
        // Highest priority: HOME_DIR env var set by scripts/init.sh
        if (const char* envHome = std::getenv("HOME_DIR")) {
            try {
                std::filesystem::path hp(envHome);
                auto p = (hp / "parameters/geometry.json").string();
                geomCandidates.emplace_back(p);
            } catch (...) {}
        }
        // current and parent relative locations
        geomCandidates.emplace_back("parameters/geometry.json");
        geomCandidates.emplace_back("../parameters/geometry.json");
        // derive from settings file path if available
        try {
            if (!jsonSettingsFile.empty()) {
                std::filesystem::path sp(jsonSettingsFile);
                auto repoRoot = sp.parent_path().parent_path(); // json/.. -> repo root
                auto fromSettings = (repoRoot / "parameters/geometry.json").string();
                geomCandidates.emplace_back(fromSettings);
            }
        } catch (...) {}
    }
    bool loadedFromFile = false;
    for (const auto &geomPath : geomCandidates) {
        try {
            std::ifstream gfin(geomPath);
            if (gfin) {
                nlohmann::json gjs; gfin >> gjs;
                if (gjs.contains("planeOffsetsMm") && gjs["planeOffsetsMm"].is_array()) {
                    for (int i = 0; i < geomDetN && i < (int)gjs["planeOffsetsMm"].size(); ++i) {
                        const auto &elt = gjs["planeOffsetsMm"][i];
                        if (elt.is_array() && elt.size() >= 2 && elt[0].is_number() && elt[1].is_number()) {
                            planeOffsets[i].first  = elt[0].get<double>(); // x0
                            planeOffsets[i].second = elt[1].get<double>(); // y0
                        }
                    }
                    LogInfo << "Loaded plane offsets from: " << geomPath << std::endl;
                    loadedFromFile = true;
                    // Remember which geometry file we used so we can read other parameters (e.g. axis ranges)
                    usedGeomPath = geomPath;
                    break;
                }
            }
        } catch (...) {
            // try next
        }
    }
    if (!loadedFromFile) {
        LogInfo << "No parameters/geometry.json found in standard locations; falling back to JSON settings if available." << std::endl;
        if (jsonSettings.contains("planeOffsetsMm") && jsonSettings["planeOffsetsMm"].is_array()) {
            for (int i = 0; i < geomDetN && i < (int)jsonSettings["planeOffsetsMm"].size(); ++i) {
                const auto &elt = jsonSettings["planeOffsetsMm"][i];
                if (elt.is_array() && elt.size() >= 2 && elt[0].is_number() && elt[1].is_number()) {
                    planeOffsets[i].first  = elt[0].get<double>(); // x0
                    planeOffsets[i].second = elt[1].get<double>(); // y0
                }
            }
            LogInfo << "Loaded plane offsets from ev-settings.json (legacy)." << std::endl;
        }
    }
    for (int ioff=0; ioff<geomDetN; ++ioff) {
        LogInfo << "Plane offset det " << ioff << ": (x0,y0) = (" << planeOffsets[ioff].first << ", " << planeOffsets[ioff].second << ") mm" << std::endl;
    }

    // 2D centers axis ranges; now sourced from parameters/geometry.json (if present)
    // Defaults aligned to requested view: X [-100, 30], Y [-60, 60]
    double centersXmin = -80.0, centersXmax = 50.0; // mm
    double centersYmin = -60.0, centersYmax = 60.0; // mm
    if (!usedGeomPath.empty()) {
        try {
            std::ifstream finAx(usedGeomPath);
            if (finAx) {
                nlohmann::json gjs2; finAx >> gjs2;
                if (gjs2.contains("centersAxisRanges") && gjs2["centersAxisRanges"].is_object()) {
                    const auto &ax = gjs2["centersAxisRanges"];
                    if (ax.contains("xmin") && ax["xmin"].is_number()) centersXmin = ax["xmin"].get<double>();
                    if (ax.contains("xmax") && ax["xmax"].is_number()) centersXmax = ax["xmax"].get<double>();
                    if (ax.contains("ymin") && ax["ymin"].is_number()) centersYmin = ax["ymin"].get<double>();
                    if (ax.contains("ymax") && ax["ymax"].is_number()) centersYmax = ax["ymax"].get<double>();
                }
            }
        } catch (...) { /* keep defaults */ }
    }
    // Enforce X min to -100 mm as requested
    // Force X-axis range regardless of geometry file per request
    centersXmin = -80.0;
    centersXmax = 50.0;

    // Optional flip of the displayed Y coordinate (useful to have channel 0 at highest Y)
    bool flipY = false;
    if (jsonSettings.contains("flipY")) {
        try {
            if (jsonSettings["flipY"].is_boolean()) flipY = jsonSettings["flipY"].get<bool>();
            else if (jsonSettings["flipY"].is_string()) {
                std::string v = jsonSettings["flipY"].get<std::string>();
                std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                flipY = (v == "true" || v == "1" || v == "yes" || v == "y");
            } else if (jsonSettings["flipY"].is_number_integer()) {
                flipY = (jsonSettings["flipY"].get<int>() != 0);
            }
        } catch (...) { /* keep default */ }
    }
    
    // Histograms to accumulate reconstructed centers (in mm)
    // Use configurable axis ranges to ensure the full distribution is visible
    const int centersBinsX = 20, centersBinsY = 20; // keep finer bins
    TH2F *h_recoCenter_3 = new TH2F("h_recoCenter_3", " (exactly 3 clusters, 1 per detector);X [mm];Y [mm]", centersBinsX, centersXmin, centersXmax, centersBinsY, centersYmin, centersYmax);
    TH2F *h_recoCenter_2to3 = new TH2F("h_recoCenter_2to3", " (2 or 3 clusters);X [mm];Y [mm]", centersBinsX, centersXmin, centersXmax, centersBinsY, centersYmin, centersYmax);
    TGraph *g_recoCenters_3 = new TGraph();
    g_recoCenters_3->SetName("g_recoCenters_3");
    g_recoCenters_3->SetTitle(" (exactly 3 clusters, 1 per detector);X [mm];Y [mm]");
    g_recoCenters_3->SetMarkerStyle(20);
    g_recoCenters_3->SetMarkerSize(0.5);
    TGraph *g_recoCenters_2to3 = new TGraph();
    g_recoCenters_2to3->SetName("g_recoCenters_2to3");
    g_recoCenters_2to3->SetTitle(" (2 or 3 clusters);X [mm];Y [mm]");
    g_recoCenters_2to3->SetMarkerStyle(20);
    g_recoCenters_2to3->SetMarkerSize(0.5);
    // Clean sample A: exactly one cluster in D0,D1,D2; cluster width <= cleanMaxWidth; D3 has no hits
    TGraph *g_cleanCenters = new TGraph();
    g_cleanCenters->SetName("g_cleanCenters");
    g_cleanCenters->SetMarkerStyle(20);
    g_cleanCenters->SetMarkerSize(0.6);
    long long cleanSampleCount = 0; // count of clean events
    // Clean sample B: exactly one cluster in exactly two detectors among D0..D2; widths <= cleanMaxWidth; D3 has no hits
    TGraph *g_cleanCenters2 = new TGraph();
    g_cleanCenters2->SetName("g_cleanCenters_twoDet");
    g_cleanCenters2->SetMarkerStyle(20);
    g_cleanCenters2->SetMarkerSize(0.6);
    long long cleanSample2Count = 0;

    // Track simple means for visibility
    double sumX_2to3 = 0.0, sumY_2to3 = 0.0; long long cnt_2to3 = 0;
    double sumX_3 = 0.0, sumY_3 = 0.0; long long cnt_3 = 0;

    // --- Preload timestamps from event_info to support 10s spill grouping ---
    // Use 'timestamp' ticks at 20 ns -> 10 s corresponds to 500,000,000 ticks
    const long long SPILL_TICKS = 500000000LL; // 10 s at 50 MHz (20 ns ticks)
    const double    TICK_PERIOD_SEC = 20e-9;   // seconds per internal timestamp tick
    std::vector<long long> evtTimestamps; evtTimestamps.reserve(limit);
    std::vector<double> evtDeltaTimesSec; evtDeltaTimesSec.reserve(limit > 0 ? (size_t)limit-1 : 0); // consecutive event deltas (sec)
    long long t0Ticks = 0; bool haveSpillTimes = false;
    {
        TTree *infoT = (TTree*) input_root_file->Get("event_info");
        if (infoT != nullptr) {
            Long64_t ts = 0;
            bool hasTs = (infoT->GetBranch("timestamp") != nullptr) && (infoT->SetBranchAddress("timestamp", &ts) >= 0);
            if (hasTs) {
                Long64_t nEvt = infoT->GetEntries();
                Long64_t nToRead = std::min<Long64_t>(nEvt, limit);
                for (Long64_t i = 0; i < nToRead; ++i) {
                    infoT->GetEntry(i);
                    evtTimestamps.emplace_back((long long)ts);
                }
                if (!evtTimestamps.empty()) {
                    t0Ticks = evtTimestamps.front();
                    haveSpillTimes = true;
                    // Build consecutive event delta times (in seconds)
                    for (size_t i = 1; i < evtTimestamps.size(); ++i) {
                        long long dtTicks = evtTimestamps[i] - evtTimestamps[i-1];
                        if (dtTicks >= 0) evtDeltaTimesSec.push_back(dtTicks * TICK_PERIOD_SEC);
                    }
                    if (nToRead < limit) {
                        LogWarning << "event_info has fewer entries (" << nToRead << ") than events processed (" << limit << "). Spill summary will be computed for the first " << nToRead << " events only." << std::endl;
                    }
                }
            } else {
                LogWarning << "'event_info' TTree is missing 'timestamp' branch; per-spill summary disabled." << std::endl;
            }
        } else {
            LogWarning << "No 'event_info' TTree found; per-spill summary disabled." << std::endl;
        }
    }

    // Aggregators for rolling spills: vector of per-spill cluster sums [D0..D3]
    std::vector<std::array<long long,4>> clustersPerSpillVec; // each entry corresponds to one 10s spill window
    bool spillActive = false; long long currentSpillStart = -1; // in ticks
    std::vector<double> spillGapSec; spillGapSec.reserve(64);   // gap between 10 s spills (sec)

    for (int entryit = 0; entryit < limit; entryit++) {

        hitsInEvent = 0;
        
        Event * this_event; // across detectors
        this_event = new Event();

        if (entryit % 10000 == 0 ) LogInfo << "Entry " << entryit << std::endl;

        this_event->SetBaseline(baseline);
        this_event->SetSigma(baseline_sigma); 
        this_event->SetNSigma(clp.getOptionVal<int>("nSigma"));

        // clear data
        for (int detit = 0; detit < nDetectors; detit++)   data->at(detit)->clear();

        for (int detit = 0; detit < nDetectors; detit++) {
            raw_events_trees.at(detit)->GetEntry(entryit);
            this_event->AddPeak(detit, *data->at(detit));
            for (int chit = 0; chit < nChannels; chit++) {
                // Skip edge channels if masking is enabled
                if (maskEdgeChannels && isEdgeChannel(chit)) {
                    continue;
                }
                // LogInfo << "DetId " << detit << ", channel " << chit << ", peak: " << this_event->GetPeak(detit, chit) << ", baseline: " << this_event->GetBaseline(detit, chit) << ", sigma: " << this_event->GetSigma(detit, chit) << "\t";
                h_rawPeak->at(detit)->at(chit)->Fill(this_event->GetPeak(detit, chit));
            }
        }

        this_event->ExtractTriggeredHits();

    if (verbose) this_event->PrintOverview();        
        if (debug) this_event->PrintInfo(); // this should rather be debug

        // Apply event-level filter for 2D centers: accept only events with exactly
        // one cluster per detector (0,1,2). A cluster is one or more hits in contiguous
        // channels within the same detector (edge-masked if enabled).
    std::vector <std::pair<int, int>> triggeredHits = this_event->GetTriggeredHits();
    int eventHitsMasked = 0;
    if (!triggeredHits.empty()){
            // Always fill per-hit histograms for all triggered (masked) hits
            for (auto &hit : triggeredHits) {
                int det = hit.first;
                int ch  = hit.second;
                if (det < 0 || det >= nDetectors) continue;
                if (maskEdgeChannels && isEdgeChannel(ch)) continue;
        eventHitsMasked++;
                float amplitude = this_event->GetPeak(det, ch) - this_event->GetBaseline(det, ch);
                h_firingChannels->at(det)->Fill(ch);
                h_amplitude->at(det)->Fill(amplitude);
                h_amplitudeVsChannel->at(det)->Fill(ch, amplitude);
            }

            // Gather masked hits per detector
            std::vector<int> detHits[4];
            for (auto &hit : triggeredHits) {
                int det = hit.first;
                int ch  = hit.second;
                if (det < 0 || det >= 4) continue;
                if (maskEdgeChannels && isEdgeChannel(ch)) continue;
                detHits[det].push_back(ch);
            }

            // Compute clusters per detector (contiguous runs)
            auto countClusters = [](std::vector<int>& chans)->std::pair<int, std::pair<int,int>>{
                if (chans.empty()) return {0, { -1, -1 }};
                std::sort(chans.begin(), chans.end());
                chans.erase(std::unique(chans.begin(), chans.end()), chans.end());
                int clusters = 0;
                int runStart = chans.front();
                int prev = chans.front();
                int cMin = runStart, cMax = prev; // track the only cluster's min/max when clusters==1
                for (size_t i=1; i<chans.size(); ++i) {
                    if (chans[i] != prev + 1) {
                        clusters++;
                        if (clusters == 1) { cMin = runStart; cMax = prev; }
                        runStart = chans[i];
                    }
                    prev = chans[i];
                }
                // end last run
                clusters++;
                if (clusters == 1) { cMin = runStart; cMax = prev; }
                return {clusters, {cMin, cMax}};
            };

            auto c0 = countClusters(detHits[0]);
            auto c1 = countClusters(detHits[1]);
            auto c2 = countClusters(detHits[2]);
            auto c3 = countClusters(detHits[3]);

            // Fill clusters-per-event histograms per detector (only when clusters > 0)
            if (c0.first > 0) h_clustersPerEvent[0]->Fill(c0.first);
            if (c1.first > 0) h_clustersPerEvent[1]->Fill(c1.first);
            if (c2.first > 0) h_clustersPerEvent[2]->Fill(c2.first);
            if (c3.first > 0) h_clustersPerEvent[3]->Fill(c3.first);

            // Rolling 10s spills anchored at first event-WITH-HITS; new spill starts at next event-with-hits after previous window closes
            if (haveSpillTimes && entryit < (int)evtTimestamps.size()) {
                // event considered "with hits" if any detector has at least one hit
                bool anyHits = (!detHits[0].empty() || !detHits[1].empty() || !detHits[2].empty() || !detHits[3].empty());
                if (anyHits) {
                    long long ts = evtTimestamps[entryit];
                    if (!spillActive) {
                        currentSpillStart = ts; spillActive = true;
                        clustersPerSpillVec.push_back({0,0,0,0});
                    } else if (ts - currentSpillStart >= SPILL_TICKS) {
                        // spill window elapsed; record gap from previous spill end to this event
                        long long prevEnd = currentSpillStart + SPILL_TICKS;
                        long long gapTicks = ts - prevEnd;
                        if (gapTicks >= 0) spillGapSec.push_back(gapTicks * TICK_PERIOD_SEC);
                        // start a new spill at this event timestamp
                        currentSpillStart = ts;
                        clustersPerSpillVec.push_back({0,0,0,0});
                    }
                    // accumulate cluster counts for this event into the current spill
                    auto &acc = clustersPerSpillVec.back();
                    acc[0] += (long long)c0.first;
                    acc[1] += (long long)c1.first;
                    acc[2] += (long long)c2.first;
                    acc[3] += (long long)c3.first;
                }
            }
            // Accept events with exactly 2 or 3 clusters total in D0..D2 (ignore D3 entirely)
            bool perDetOk = (c0.first <= 1 && c1.first <= 1 && c2.first <= 1);
            int totalClusters = (c0.first > 0) + (c1.first > 0) + (c2.first > 0);
            bool acceptTwoOrThree = perDetOk && (totalClusters == 2 || totalClusters == 3);
            bool acceptExactlyThree = (c0.first == 1 && c1.first == 1 && c2.first == 1);
            // Count events that have clusters in at least two different detectors (no per-detector multiplicity restriction)
            if (totalClusters >= 2) {
                eventsTwoDetOrMore++;
            }
            if (acceptTwoOrThree) {
                // Use cluster center channel per detector for (x,y) interpolation
                int firstTrigChan[geomDetN];
                firstTrigChan[0] = (c0.second.first >= 0 ? (c0.second.first + c0.second.second)/2 : -1);
                firstTrigChan[1] = (c1.second.first >= 0 ? (c1.second.first + c1.second.second)/2 : -1);
                firstTrigChan[2] = (c2.second.first >= 0 ? (c2.second.first + c2.second.second)/2 : -1);
                triggeredEvents++;
                hitsInEvent = (int)(detHits[0].size() + detHits[1].size() + detHits[2].size());
            // Apply cluster width cut for used detectors to enforce clean geometry on main displays
            int w0 = (c0.second.first >= 0 && c0.first==1) ? (c0.second.second - c0.second.first + 1) : 0;
            int w1 = (c1.second.first >= 0 && c1.first==1) ? (c1.second.second - c1.second.first + 1) : 0;
            int w2 = (c2.second.first >= 0 && c2.first==1) ? (c2.second.second - c2.second.first + 1) : 0;
            bool widthsOK = true;
            if (c0.first==1) widthsOK = widthsOK && (w0>0 && w0<=cleanMaxWidth);
            if (c1.first==1) widthsOK = widthsOK && (w1>0 && w1<=cleanMaxWidth);
            if (c2.first==1) widthsOK = widthsOK && (w2>0 && w2<=cleanMaxWidth);
            // Reconstruct (x,y) from projections u_i = x cosθ_i + y sinθ_i, using absolute/global coordinates.
            // Accumulate for least squares if >=2 detectors
            double Scc=0, Sss=0, Scs=0, Su_c=0, Su_s=0; int used=0;
            // Temporary storage for exactly-2-detector direct solve
            double c_a=0,s_a=0,u_a=0,c_b=0,s_b=0,u_b=0; int pairCount=0;
        for (int i=0;i<geomDetN;i++) {
                if (firstTrigChan[i] >= 0) {
            // Channel-centered u: middle channel maps to u=0 (e.g., det2 center at y=0)
            // Map channel 0 to highest +u (about +5 cm), channel N-1 to lowest -u (about -5 cm)
            double u_meas = ( (nChannels/2.0) - (firstTrigChan[i] + 0.5) ) * channelPitch; // mm
            // Project along the normal to the strips
            double c = cos(normalAnglesRad[i]);
            double s = sin(normalAnglesRad[i]);
            // Account for detector plane offsets: x0,y0 shift into RHS as u_eff = u_meas + x0*c + y0*s
            double u = u_meas + (planeOffsets[i].first * c + planeOffsets[i].second * s);
                    Scc += c*c; Sss += s*s; Scs += c*s; Su_c += u*c; Su_s += u*s; used++;
                    if (pairCount==0) {c_a=c; s_a=s; u_a=u; pairCount=1;} else if (pairCount==1){c_b=c; s_b=s; u_b=u; pairCount=2;}
                }
            }
            double cx=0, cy=0; bool haveXY=false;
            if (used >= 2) {
                if (used == 2) {
                    // Direct 2x2 solve
                    double det = c_a*s_b - s_a*c_b; // = sin(theta_b - theta_a)
                    if (fabs(det) > 1e-9) {
                        cx = ( u_a*s_b - s_a*u_b)/det;
                        cy = ( c_a*u_b - u_a*c_b)/det;
                        haveXY = true;
                    }
                } else { // used ==3 (or more if extended)
                    double det = Scc*Sss - Scs*Scs;
                    if (fabs(det) > 1e-12) {
                        cx = ( Sss*Su_c - Scs*Su_s)/det;
                        cy = ( Scc*Su_s - Scs*Su_c)/det;
                        haveXY = true;
                    }
                }
            }
            if (haveXY && widthsOK) {
                if (flipY) cy = -cy; // flip Y orientation if requested
                // Fill 2-or-3 clusters map always
                h_recoCenter_2to3->Fill(cx, cy);
                g_recoCenters_2to3->SetPoint(g_recoCenters_2to3->GetN(), cx, cy);
                sumX_2to3 += cx; sumY_2to3 += cy; cnt_2to3++;
                selected2to3Count++;
                // Additionally fill the exactly-3-clusters map when applicable
                if (acceptExactlyThree) {
                    h_recoCenter_3->Fill(cx, cy);
                    g_recoCenters_3->SetPoint(g_recoCenters_3->GetN(), cx, cy);
                    sumX_3 += cx; sumY_3 += cy; cnt_3++;
                    selected3Count++;
                    // Clean sample A selection: widths <= cleanMaxWidth (D3 ignored)
                    if (w0>0 && w1>0 && w2>0 && w0<=cleanMaxWidth && w1<=cleanMaxWidth && w2<=cleanMaxWidth) {
                        g_cleanCenters->SetPoint(g_cleanCenters->GetN(), cx, cy);
                        cleanSampleCount++;
                    }
                }
                // Clean sample B (exactly two detectors among D0..D2 have exactly 1 cluster; widths <= cleanMaxWidth)
                if (!acceptExactlyThree) {
                    int ones = (c0.first==1) + (c1.first==1) + (c2.first==1);
                    if (ones == 2) {
                        bool widthsOK = true;
                        if (c0.first==1) widthsOK = widthsOK && (w0>0 && w0<=cleanMaxWidth);
                        if (c1.first==1) widthsOK = widthsOK && (w1>0 && w1<=cleanMaxWidth);
                        if (c2.first==1) widthsOK = widthsOK && (w2>0 && w2<=cleanMaxWidth);
                        if (widthsOK) {
                            g_cleanCenters2->SetPoint(g_cleanCenters2->GetN(), cx, cy);
                            cleanSample2Count++;
                        }
                    }
                }
            }
            }
    }
    // Fill hits-per-event with the number of masked triggered hits (not cluster-gated)
    h_hitsInEvent->Fill(eventHitsMasked);
    if (eventHitsMasked > 0) { eventsWithHits++; }
    // Do not constrain other histograms; only centers are gated by the cluster criterion

        if (debug) this_event->PrintValidHits();      
        
        if (verbose) LogInfo << "Stored entry " << entryit << " in the vector of Events" << std::endl;

        delete this_event;

    }

    LogInfo << "Read all entries" << std::endl;
    if (cnt_2to3 > 0) {
        LogInfo << "Mean center (2or3): (" << (sumX_2to3/cnt_2to3) << ", " << (sumY_2to3/cnt_2to3) << ") mm over " << cnt_2to3 << " pts" << std::endl;
    }
    if (cnt_3 > 0) {
        LogInfo << "Mean center (exactly3): (" << (sumX_3/cnt_3) << ", " << (sumY_3/cnt_3) << ") mm over " << cnt_3 << " pts" << std::endl;
    }

    LogInfo << "Number of triggered events: " << (double) triggeredEvents/limit *100 << "%" << std::endl;
    LogInfo << "Events with hits (any detector): " << (double) eventsWithHits/limit * 100 << "% (" << eventsWithHits << "/" << limit << ")" << std::endl;
    
    ///////////////////////////
    
    // plots

    // create a canvas
    LogInfo << "Creating canvas" << std::endl;
    std::string runNumber = "99999"; // fallback value
    if (clp.isOptionTriggered("runNumber")) {
        runNumber = clp.getOptionVal<std::string>("runNumber");
        LogInfo << "Using provided run number: " << runNumber << std::endl;
    } else {
        LogInfo << "No run number provided, using fallback: " << runNumber << std::endl;
    }

    // Populate sigma and baseline TGraphs with the correct values per channel
    LogInfo << "Populating sigma and baseline graphs" << std::endl;
    for (int detit = 0; detit < nDetectors; detit++) {
        for (int chit = 0; chit < nChannels; chit++) {
            g_sigma->at(detit)->SetPoint(chit, chit, baseline_sigma.at(detit).at(chit));
            g_baseline->at(detit)->SetPoint(chit, chit, baseline.at(detit).at(chit));
        }
        // Update graph titles with run number
        g_sigma->at(detit)->SetTitle(Form("Sigma (Detector %d) - Run %s", detit, runNumber.c_str()));
        g_baseline->at(detit)->SetTitle(Form("Baseline (Detector %d) - Run %s", detit, runNumber.c_str()));
    }

    // Update histogram titles with run number
    LogInfo << "Updating histogram titles with run number" << std::endl;
    for (int i = 0; i < nDetectors; i++) {
        h_firingChannels->at(i)->SetTitle(Form("Firing channels (Detector %d) - Run %s", i, runNumber.c_str()));
        h_amplitude->at(i)->SetTitle(Form("Amplitude (Detector %d) - Run %s", i, runNumber.c_str()));
        h_amplitudeVsChannel->at(i)->SetTitle(Form("Amplitude vs Channel (Detector %d) - Run %s", i, runNumber.c_str()));
        
        // Update raw peak histogram titles if in verbose mode
        if (verbose) {
            for (int j = 0; j < nChannels; j++) {
                h_rawPeak->at(i)->at(j)->SetTitle(Form("Raw peak (Detector %d, Channel %d) - Run %s", i, j, runNumber.c_str()));
            }
        }
    }
    h_hitsInEvent->SetTitle(Form("Hits in event - Run %s", runNumber.c_str()));

    // Update canvas titles
    TCanvas *c_channelsFiring = new TCanvas(Form("c_channelsFiring_Run%s", runNumber.c_str()), Form("Channels Firing (Run %s)", runNumber.c_str()), 800, 600);
    c_channelsFiring->Divide(2, 2);

    TCanvas *c_sigma = new TCanvas(Form("c_sigma_Run%s", runNumber.c_str()), Form("Sigma (Run %s)", runNumber.c_str()), 800, 600);
    c_sigma->Divide(2, 2);

    TCanvas *c_baseline = new TCanvas(Form("c_baseline_Run%s", runNumber.c_str()), Form("Baseline (Run %s)", runNumber.c_str()), 800, 600);
    c_baseline->Divide(2, 2);

    // Update raw peak canvas titles
    std::vector<TCanvas*> *c_rawPeak = new std::vector<TCanvas*>;
    c_rawPeak->reserve(6);
    if (verbose) {
        for (int i = 0; i < 6; i++) {
            TCanvas *this_c_rawPeak = new TCanvas(Form("c_rawPeak%d_Run%s", i, runNumber.c_str()), Form("Raw Peak %d (Run %s)", i, runNumber.c_str()), 800, 600);
            this_c_rawPeak->Divide(8, 8);
            c_rawPeak->emplace_back(this_c_rawPeak);
        }
    }

    TCanvas *c_amplitude = new TCanvas(Form("c_amplitude_Run%s", runNumber.c_str()), Form("Amplitude (Run %s)", runNumber.c_str()), 800, 600);
    c_amplitude->Divide(2, 2);

    TCanvas *c_amplitudeVsChannel = new TCanvas(Form("c_amplitudeVsChannel_Run%s", runNumber.c_str()), Form("Amplitude vs Channel (Run %s)", runNumber.c_str()), 800, 600);
    c_amplitudeVsChannel->Divide(2, 2);

    TCanvas *c_hitsInEvent = new TCanvas(Form("c_hitsInEvent_Run%s", runNumber.c_str()), Form("Hits in Event (Run %s)", runNumber.c_str()), 800, 600);

    // --- New plots: time distances ---
    TH1F *h_evtDeltaT = nullptr; TCanvas *c_evtDeltaT = nullptr;
    TH1F *h_spillGaps = nullptr; TCanvas *c_spillGaps = nullptr;
    if (!evtDeltaTimesSec.empty()) {
        double maxDt = *std::max_element(evtDeltaTimesSec.begin(), evtDeltaTimesSec.end());
        if (maxDt <= 0) maxDt = 1.0;
        int nbins = 200;
        h_evtDeltaT = new TH1F(Form("h_evtDeltaT_%s", runNumber.c_str()), Form("Delta between consecutive events (Run %s);#Delta t [s];Entries", runNumber.c_str()), nbins, 0.0, maxDt*1.05);
        for (double v : evtDeltaTimesSec) h_evtDeltaT->Fill(v);
        c_evtDeltaT = new TCanvas(Form("c_evtDeltaT_Run%s", runNumber.c_str()), Form("Event #Delta t (Run %s)", runNumber.c_str()), 800, 600);
        c_evtDeltaT->cd(); h_evtDeltaT->Draw(); c_evtDeltaT->Update();
    }
    if (!spillGapSec.empty()) {
        double maxGap = *std::max_element(spillGapSec.begin(), spillGapSec.end());
        if (maxGap <= 0) maxGap = 1.0;
        int nbins = 100;
        h_spillGaps = new TH1F(Form("h_spillGaps_%s", runNumber.c_str()), Form("Gap between 10 s spills (Run %s);Gap [s];Entries", runNumber.c_str()), nbins, 0.0, maxGap*1.05);
        for (double v : spillGapSec) h_spillGaps->Fill(v);
        c_spillGaps = new TCanvas(Form("c_spillGaps_Run%s", runNumber.c_str()), Form("Spill gap #Delta t (Run %s)", runNumber.c_str()), 800, 600);
        c_spillGaps->cd(); h_spillGaps->Draw(); c_spillGaps->Update();
    }

    // Build timestamp vs trigger NUMBER graph, and external timestamp vs trigger, if available
    TGraph *g_evtVsTime = nullptr;
    TCanvas *c_evtVsTime = nullptr;
    double timeMin = 0, timeMax = 0;
    TGraph *g_extEvtVsTime = nullptr;
    TCanvas *c_extEvtVsTime = nullptr;
    double extTimeMin = 0, extTimeMax = 0;
    {
        // Prefer the new event_info tree if present
        TTree *infoT = (TTree*) input_root_file->Get("event_info");
        if (infoT != nullptr) {
            Long64_t ts = 0; Long64_t ext_ts = 0; Long64_t trigId = 0; Long64_t trigNum = 0; Int_t idx = 0;
            bool hasTs = (infoT->GetBranch("timestamp") != nullptr) && (infoT->SetBranchAddress("timestamp", &ts) >= 0);
            bool hasExt = (infoT->GetBranch("ext_timestamp") != nullptr) && (infoT->SetBranchAddress("ext_timestamp", &ext_ts) >= 0);
            bool hasTrigId = (infoT->GetBranch("trigger_id") != nullptr) && (infoT->SetBranchAddress("trigger_id", &trigId) >= 0);
            bool hasTrigNum = (infoT->GetBranch("trigger_number") != nullptr) && (infoT->SetBranchAddress("trigger_number", &trigNum) >= 0);
            bool hasIndex = (infoT->GetBranch("event_index") != nullptr) && (infoT->SetBranchAddress("event_index", &idx) >= 0);
            if (hasTs && (hasTrigNum || hasTrigId)) {
                Long64_t nEvt = infoT->GetEntries();
                Long64_t nToRead = std::min<Long64_t>(nEvt, limit);
                g_evtVsTime = new TGraph();
                g_evtVsTime->SetName("g_timeVsTriggerNumber");
                g_evtVsTime->SetTitle(Form("Timestamp vs Trigger Number (Run %s);Time since start [ticks];Trigger Number", runNumber.c_str()));
                g_evtVsTime->SetMarkerStyle(20);
                g_evtVsTime->SetMarkerSize(0.6);
                Long64_t t0 = 0; bool t0set = false; timeMin = 0; timeMax = 0;
                for (Long64_t i = 0; i < nToRead; ++i) {
                    infoT->GetEntry(i);
                    if (!t0set) { t0 = ts; t0set = true; }
                    double dt = double((ts >= t0) ? (ts - t0) : 0LL);
                    if (i == 0) { timeMin = dt; timeMax = dt; }
                    else { if (dt < timeMin) timeMin = dt; if (dt > timeMax) timeMax = dt; }
                    double y = hasTrigNum ? double(trigNum) : double(trigId);
                    g_evtVsTime->SetPoint(g_evtVsTime->GetN(), dt, y);
                }
                c_evtVsTime = new TCanvas(Form("c_timeVsTriggerNumber_Run%s", runNumber.c_str()), Form("Timestamp vs Trigger Number (Run %s)", runNumber.c_str()), 800, 600);
                c_evtVsTime->cd();
                g_evtVsTime->Draw("AP");
                if (timeMax > timeMin) {
                    g_evtVsTime->GetXaxis()->SetLimits(timeMin - 0.01*(timeMax-timeMin), timeMax + 0.01*(timeMax-timeMin));
                }
                c_evtVsTime->Update();
                LogInfo << "Built Timestamp vs Trigger Number graph from 'event_info' TTree (" << nToRead << " points)." << std::endl;
            } else if (hasTs && !(hasTrigNum || hasTrigId)) {
                LogWarning << "'event_info' TTree has no trigger_number or trigger_id. Skipping Timestamp vs Trigger plot." << std::endl;
            }

            // Build External Timestamp vs Trigger plot if ext_timestamp is available
            if (hasExt && (hasTrigNum || hasTrigId)) {
                Long64_t nEvt = infoT->GetEntries();
                Long64_t nToRead = std::min<Long64_t>(nEvt, limit);
                g_extEvtVsTime = new TGraph();
                g_extEvtVsTime->SetName("g_extTimeVsTriggerNumber");
                g_extEvtVsTime->SetTitle(Form("External timestamp vs Trigger Number (Run %s);External time since start [ticks];Trigger Number", runNumber.c_str()));
                g_extEvtVsTime->SetMarkerStyle(20);
                g_extEvtVsTime->SetMarkerSize(0.6);
                Long64_t ext0 = 0; bool ext0set = false; extTimeMin = 0; extTimeMax = 0;
                for (Long64_t i = 0; i < nToRead; ++i) {
                    infoT->GetEntry(i);
                    if (!ext0set) { ext0 = ext_ts; ext0set = true; }
                    double dt = double((ext_ts >= ext0) ? (ext_ts - ext0) : 0LL);
                    if (i == 0) { extTimeMin = dt; extTimeMax = dt; }
                    else { if (dt < extTimeMin) extTimeMin = dt; if (dt > extTimeMax) extTimeMax = dt; }
                    double y = hasTrigNum ? double(trigNum) : double(trigId);
                    g_extEvtVsTime->SetPoint(g_extEvtVsTime->GetN(), dt, y);
                }
                c_extEvtVsTime = new TCanvas(Form("c_extTimeVsTriggerNumber_Run%s", runNumber.c_str()), Form("External timestamp vs Trigger Number (Run %s)", runNumber.c_str()), 800, 600);
                c_extEvtVsTime->cd();
                g_extEvtVsTime->Draw("AP");
                if (extTimeMax > extTimeMin) {
                    g_extEvtVsTime->GetXaxis()->SetLimits(extTimeMin - 0.01*(extTimeMax-extTimeMin), extTimeMax + 0.01*(extTimeMax-extTimeMin));
                }
                c_extEvtVsTime->Update();
                LogInfo << "Built External timestamp vs Trigger Number graph from 'event_info' TTree (" << nToRead << " points)." << std::endl;
            }
        } else {
            // Legacy support: "events" TTree if present
            TTree *eventsT = (TTree*) input_root_file->Get("events");
            if (eventsT != nullptr) {
                LogWarning << "Legacy 'events' TTree has no trigger_number. Skipping Timestamp vs Trigger plot." << std::endl;
            } else {
                LogWarning << "No 'event_info' or 'events' TTree found in ROOT file. Skipping Timestamp plot." << std::endl;
            }
        }
    }

    // Update 2D heatmap titles with percentages and prepare canvases with projections
    {
    double pTrig3 = (triggeredEvents > 0) ? (100.0 * (double)selected3Count / (double)triggeredEvents) : 0.0;
    double pTot3  = (limit > 0) ? (100.0 * (double)selected3Count / (double)limit) : 0.0;
    std::string title3 = Form("Tracking (1/Det)  [%.1f%% triggers %.1f%% all];",
                   pTrig3, pTot3);
        h_recoCenter_3->SetTitle(title3.c_str());

    // Build canvas with main pad + bottom (ProjectionX) + left (ProjectionY)
    TCanvas *c = new TCanvas("c_recoCenter_3", " (exactly 3)", 980, 820);
    // Pads: left (0,0.2)-(0.2,1), bottom (0.2,0)-(1,0.2), main (0.2,0.2)-(1,1)
    TPad *padLeft3   = new TPad("padLeft3",   "padLeft3",   0.0, 0.2, 0.2, 1.0);
    TPad *padBottom3 = new TPad("padBottom3", "padBottom3", 0.2, 0.0, 1.0, 0.2);
    TPad *padMain3   = new TPad("padMain3",   "padMain3",   0.2, 0.2, 1.0, 1.0);
    // Increase top margin to avoid clipping long titles
    padMain3->SetRightMargin(0.15); padMain3->SetTopMargin(0.16); padMain3->SetBottomMargin(0.12); padMain3->SetLeftMargin(0.12);
    padBottom3->SetTopMargin(0.18); padBottom3->SetBottomMargin(0.35); padBottom3->SetLeftMargin(0.12); padBottom3->SetRightMargin(0.15);
    padLeft3->SetRightMargin(0.05); padLeft3->SetTopMargin(0.05); padLeft3->SetBottomMargin(0.12); padLeft3->SetLeftMargin(0.28);
    c->cd();
    padMain3->Draw(); padBottom3->Draw(); padLeft3->Draw();
        // Main 2D plot
        padMain3->cd();
    h_recoCenter_3->GetXaxis()->SetTitle("X [mm]");
    h_recoCenter_3->GetYaxis()->SetTitle("Y [mm]");
    h_recoCenter_3->Draw("COLZ");
        // Mark global origin for reference
        TMarker *centerMarker3 = new TMarker(0.0, 0.0, kFullCircle);
        centerMarker3->SetMarkerColor(kRed);
        centerMarker3->SetMarkerSize(1.2);
        centerMarker3->Draw("SAME");
    // Bottom projection X
    padBottom3->cd();
        TH1D *projX3 = h_recoCenter_3->ProjectionX("h_projX_3");
    projX3->SetTitle("");
    projX3->GetXaxis()->SetLabelSize(0.10);
        projX3->GetYaxis()->SetTitle("Entries");
        projX3->GetYaxis()->SetTitleSize(0.08);
        projX3->GetYaxis()->SetLabelSize(0.08);
    projX3->SetStats(0);
    projX3->SetFillStyle(0);
        projX3->Draw("HIST");
    // Left projection Y (tilted 90 degrees to look like a sideways projection)
    padLeft3->cd();
        TH1D *projY3 = h_recoCenter_3->ProjectionY("h_projY_3");
    projY3->SetTitle("");
    projY3->GetXaxis()->SetTitle("Entries");
    projY3->GetXaxis()->SetTitleSize(0.08);
    projY3->GetXaxis()->SetLabelSize(0.08);
    projY3->GetYaxis()->SetLabelSize(0.10);
    projY3->SetStats(0);
    projY3->SetFillStyle(0);
    projY3->Draw("HBAR");
        c->Update();
    }

    {
    double pTrig23 = (triggeredEvents > 0) ? (100.0 * (double)selected2to3Count / (double)triggeredEvents) : 0.0;
    double pTot23  = (limit > 0) ? (100.0 * (double)selected2to3Count / (double)limit) : 0.0;
    std::string title23 = Form("Tracking (2–3, w<=%d)  [%.1f%% triggers %.1f%% all];X [mm];Y [mm]",
                    cleanMaxWidth, pTrig23, pTot23);
        h_recoCenter_2to3->SetTitle(title23.c_str());

    TCanvas *c = new TCanvas("c_recoCenter_2to3", " (2 or 3)", 980, 820);
    TPad *padLeft23   = new TPad("padLeft23",   "padLeft23",   0.0, 0.2, 0.2, 1.0);
    TPad *padBottom23 = new TPad("padBottom23", "padBottom23", 0.2, 0.0, 1.0, 0.2);
    TPad *padMain23   = new TPad("padMain23",   "padMain23",   0.2, 0.2, 1.0, 1.0);
    // Increase top margin to avoid clipping long titles
    padMain23->SetRightMargin(0.15); padMain23->SetTopMargin(0.16); padMain23->SetBottomMargin(0.12); padMain23->SetLeftMargin(0.12);
    padBottom23->SetTopMargin(0.18); padBottom23->SetBottomMargin(0.35); padBottom23->SetLeftMargin(0.12); padBottom23->SetRightMargin(0.15);
    padLeft23->SetRightMargin(0.05); padLeft23->SetTopMargin(0.05); padLeft23->SetBottomMargin(0.12); padLeft23->SetLeftMargin(0.28);
    c->cd();
    padMain23->Draw(); padBottom23->Draw(); padLeft23->Draw();
        // Main 2D plot
        padMain23->cd();
    h_recoCenter_2to3->GetXaxis()->SetTitle("X [mm]");
    h_recoCenter_2to3->GetYaxis()->SetTitle("Y [mm]");
    h_recoCenter_2to3->Draw("COLZ");
        TMarker *centerMarker23 = new TMarker(0.0, 0.0, kFullCircle);
        centerMarker23->SetMarkerColor(kRed);
        centerMarker23->SetMarkerSize(1.2);
        centerMarker23->Draw("SAME");
    // Bottom X projection
    padBottom23->cd();
        TH1D *projX23 = h_recoCenter_2to3->ProjectionX("h_projX_23");
    projX23->SetTitle("");
        projX23->GetXaxis()->SetLabelSize(0.10);
        projX23->GetYaxis()->SetTitle("Entries");
        projX23->GetYaxis()->SetTitleSize(0.08);
        projX23->GetYaxis()->SetLabelSize(0.08);
    projX23->SetStats(0);
    projX23->SetFillStyle(0);
        projX23->Draw("HIST");
    padLeft23->cd();
        TH1D *projY23 = h_recoCenter_2to3->ProjectionY("h_projY_23");
    projY23->SetTitle("");
        projY23->GetXaxis()->SetTitle("Entries");
        projY23->GetXaxis()->SetTitleSize(0.08);
        projY23->GetXaxis()->SetLabelSize(0.08);
        projY23->GetYaxis()->SetLabelSize(0.10);
    projY23->SetStats(0);
    projY23->SetFillStyle(0);
    projY23->Draw("HBAR");
        c->Update();
    }

    // Scatter-only plots (no histogram underneath) for reconstructed centers
    TCanvas *c_recoCenterScatter_3 = new TCanvas("c_recoCenterScatter_3", "Interpolated Centers Scatter (exactly 3)", 740, 560);
    c_recoCenterScatter_3->cd();
    gPad->SetTopMargin(0.16);
    {
    double pTrig3 = (triggeredEvents > 0) ? (100.0 * (double)selected3Count / (double)triggeredEvents) : 0.0;
    double pTot3  = (limit > 0) ? (100.0 * (double)selected3Count / (double)limit) : 0.0;
    std::string ttl3 = Form("Scatter (1/Det)  [%.1f%% triggers %.1f%% all];X [mm];Y [mm]",
                 pTrig3, pTot3);
        TH2F *frame3 = new TH2F("frame3", ttl3.c_str(), 10, centersXmin, centersXmax, 10, centersYmin, centersYmax);
        frame3->SetStats(0);
        frame3->Draw();
        g_recoCenters_3->SetMarkerStyle(20);
        g_recoCenters_3->SetMarkerSize(0.6);
        if (g_recoCenters_3->GetN() > 0) {
            g_recoCenters_3->Draw("P SAME");
        } else {
            TLatex l; l.SetNDC(true); l.SetTextSize(0.05); l.DrawLatex(0.3,0.5,"No points");
        }
        TMarker *m0 = new TMarker(0.0, 0.0, kFullCircle);
        m0->SetMarkerColor(kRed);
        m0->SetMarkerSize(1.2);
        m0->Draw("SAME");
        c_recoCenterScatter_3->Update();
    }
    TCanvas *c_recoCenterScatter_2to3 = new TCanvas("c_recoCenterScatter_2to3", "Interpolated Centers Scatter (2 or 3)", 740, 560);
    c_recoCenterScatter_2to3->cd();
    gPad->SetTopMargin(0.16);
    {
    double pTrig23 = (triggeredEvents > 0) ? (100.0 * (double)selected2to3Count / (double)triggeredEvents) : 0.0;
    double pTot23  = (limit > 0) ? (100.0 * (double)selected2to3Count / (double)limit) : 0.0;
    std::string ttl23 = Form("Scatter (2–3)  [%.1f%% triggers %.1f%% all];X [mm];Y [mm]",
                 pTrig23, pTot23);
        TH2F *frame23 = new TH2F("frame23", ttl23.c_str(), 10, centersXmin, centersXmax, 10, centersYmin, centersYmax);
        frame23->SetStats(0);
        frame23->Draw();
        g_recoCenters_2to3->SetMarkerStyle(20);
        g_recoCenters_2to3->SetMarkerSize(0.6);
        if (g_recoCenters_2to3->GetN() > 0) {
            g_recoCenters_2to3->Draw("P SAME");
        } else {
            TLatex l; l.SetNDC(true); l.SetTextSize(0.05); l.DrawLatex(0.3,0.5,"No points");
        }
        TMarker *m0b = new TMarker(0.0, 0.0, kFullCircle);
        m0b->SetMarkerColor(kRed);
        m0b->SetMarkerSize(1.2);
        m0b->Draw("SAME");
        c_recoCenterScatter_2to3->Update();
    }

    // Removed clean sample scatter-only displays per request

    LogInfo << "Drawing histograms" << std::endl;
    for (int i = 0; i < nDetectors; i++) {
        c_channelsFiring->cd(i+1);
    // Use log scale for counts and ensure a positive minimum
    gPad->SetLogy();
    h_firingChannels->at(i)->SetMinimum(0.5);
    h_firingChannels->at(i)->Draw();
    }
    c_channelsFiring->Update();

    // Print top-N spike channels per detector for quick inspection
    const int topN = 10;
    LogInfo << "Top firing channels per detector (channel:count)" << std::endl;
    for (int i = 0; i < nDetectors; i++) {
        std::vector<std::pair<int,double>> counts; counts.reserve(nChannels);
        for (int b = 1; b <= nChannels; ++b) {
            double c = h_firingChannels->at(i)->GetBinContent(b);
            if (c > 0) counts.emplace_back(b-1, c); // channel index = bin-1
        }
        std::sort(counts.begin(), counts.end(), [](const auto& a, const auto& b){return a.second > b.second;});
        LogInfo << "  Detector " << i << ": ";
        int limitN = std::min(topN, (int)counts.size());
        for (int k = 0; k < limitN; ++k) {
            LogInfo << counts[k].first << ":" << (long long)counts[k].second << (k==limitN-1?"":"  ");
        }
        LogInfo << std::endl;
    }

    for (int i = 0; i < nDetectors; i++) {
        c_sigma->cd(i+1);
        g_sigma->at(i)->Draw("AP");
    }

    for (int i = 0; i < nDetectors; i++) {
        c_baseline->cd(i+1);
        g_baseline->at(i)->Draw("AP");
    }

    // note that only raw peaks of detector 0 are being plotted
    if (verbose){
        for (int ch = 0; ch < nChannels; ch++) {
            c_rawPeak->at(ch/64)->cd(ch%64+1);
            h_rawPeak->at(0)->at(ch)->Draw();
        }
    }


    for (int i = 0; i < nDetectors; i++) {
        c_amplitude->cd(i+1);
        h_amplitude->at(i)->Draw();
    }

    for (int i = 0; i < nDetectors; i++) {
        c_amplitudeVsChannel->cd(i+1);
        h_amplitudeVsChannel->at(i)->Draw("COLZ");
    }

    c_hitsInEvent->cd();
    gPad->SetLogy();
    h_hitsInEvent->Draw();

    // create a pdf report containing firing channels, sigma and amplitude
    std::string outputDir = clp.getOptionVal<std::string>("outputDir");
    // LogInfo << "Output directory: " << outputDir << std::endl;
    // output filename same as input root file, but remove .root and create single PDF report
    std::string input_file_base = input_root_filename.substr(input_root_filename.find_last_of("/\\") + 1);
    size_t lastdot = input_file_base.find_last_of(".");
    if (lastdot != std::string::npos) {
        input_file_base = input_file_base.substr(0, lastdot);
    }
    // Single PDF report filename
    std::string output_filename_report = outputDir + "/" + input_file_base + "_" + std::to_string(clp.getOptionVal<int>("nSigma")) + "sigma_report.pdf";

    LogInfo << "Output PDF report: " << output_filename_report << std::endl;
    
    // Check output directory permissions
    if (system(("test -w " + outputDir).c_str()) != 0) {
        LogError << "Output directory " << outputDir << " is not writable!" << std::endl;
        return 1;
    }

    LogInfo << "Saving canvases to multi-page pdf report" << std::endl;
    
    // Ensure output directory exists
    system(("mkdir -p " + outputDir).c_str());
    
    // save the canvases to a multi-page pdf report with error handling
    try {
        // Check that all canvases are valid before attempting to save
        if (!c_channelsFiring || !c_sigma || !c_baseline || !c_amplitude || !c_amplitudeVsChannel || !c_hitsInEvent) {
            LogError << "One or more canvases are null, cannot save plots" << std::endl;
            return 1;
        }
        
        LogInfo << "Creating multi-page PDF report..." << std::endl;
        // Page 1: Summary page with general run information
        TCanvas *c_summary = new TCanvas(Form("c_summary_Run%s", runNumber.c_str()), Form("Run %s Summary", runNumber.c_str()), 800, 600);
        c_summary->cd();
        TLatex lat;
        lat.SetNDC(true);
        lat.SetTextSize(0.045);
        double y = 0.90; const double dy = 0.06;
        lat.DrawLatex(0.12, y, Form("Run %s summary", runNumber.c_str())); y-=dy;
        // Run start timestamp from filename (+2h), placed right after the title
        {
            auto isAllDigits = [](const std::string &s){ return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit); };
            std::string base = input_file_base; // e.g., SCD_RUN00488_BEAM_20250901_175435
            size_t posTime = base.rfind('_');
            if (posTime != std::string::npos && posTime+1 < base.size()) {
                std::string timeTok = base.substr(posTime+1); // HHMMSS
                std::string beforeTime = base.substr(0, posTime);
                size_t posDate = beforeTime.rfind('_');
                if (posDate != std::string::npos && posDate+1 < beforeTime.size()) {
                    std::string dateTok = beforeTime.substr(posDate+1); // YYYYMMDD
                    if (dateTok.size()==8 && timeTok.size()>=4 && isAllDigits(dateTok) && isAllDigits(timeTok)) {
                        int year = std::stoi(dateTok.substr(0,4));
                        int month = std::stoi(dateTok.substr(4,2));
                        int day = std::stoi(dateTok.substr(6,2));
                        int hour = std::stoi(timeTok.substr(0,2));
                        int minute = std::stoi(timeTok.substr(2,2));
                        auto isLeap = [](int y){ return (y%4==0 && y%100!=0) || (y%400==0); };
                        auto daysInMonth = [&](int y,int m){ static int d[12]={31,28,31,30,31,30,31,31,30,31,30,31}; return m==2 ? (isLeap(y)?29:28) : d[m-1]; };
                        hour += 2; // add +2h offset
                        if (hour >= 24) { hour -= 24; day += 1; int dim = daysInMonth(year,month); if (day>dim){ day=1; month+=1; if (month>12){ month=1; year+=1; } } }
                        lat.DrawLatex(0.12, y, Form("This run started on %04d/%02d/%02d at %02d:%02d", year, month, day, hour, minute)); y-=dy;
                    }
                }
            }
        }
        lat.DrawLatex(0.12, y, Form("Input: %s", input_file_base.c_str())); y-=dy;
        lat.DrawLatex(0.12, y, Form("Calibration: %s", clp.getOptionVal<std::string>("inputCalFile").substr(clp.getOptionVal<std::string>("inputCalFile").find_last_of("/\\")+1).c_str())); y-=dy;
        lat.DrawLatex(0.12, y, Form("Total triggers: %d", limit)); y-=dy;
        lat.DrawLatex(0.12, y, Form("Triggers with BM hits: %.2f%% (%d/%d)", (limit>0)?(100.0*eventsWithHits/limit):0.0, eventsWithHits, limit)); y-=dy;
        lat.DrawLatex(0.12, y, Form("Triggers with >=1 clusters in >=2 detectors: %.2f%% (%d/%d)", (limit>0)?(100.0*eventsTwoDetOrMore/limit):0.0, eventsTwoDetOrMore, limit)); y-=dy;
        lat.DrawLatex(0.12, y, Form("Triggers with 1 cluster in 2 or 3 detectors: %.2f%% (%d/%d)", (limit>0)?(100.0*triggeredEvents/limit):0.0, triggeredEvents, limit)); y-=dy;
    // Removed geometry path line per request
        if (cnt_2to3>0) { lat.DrawLatex(0.12, y, Form("Mean center (2or3): (%.2f, %.2f) mm", sumX_2to3/cnt_2to3, sumY_2to3/cnt_2to3)); y-=dy; }
        if (cnt_3>0)    { lat.DrawLatex(0.12, y, Form("Mean center (exactly3): (%.2f, %.2f) mm", sumX_3/cnt_3, sumY_3/cnt_3)); y-=dy; }
        // Average clusters per 10 s spill across the whole run (rolling windows)
        if (!clustersPerSpillVec.empty()) {
            long long nSpills = (long long)clustersPerSpillVec.size();
            long long sumD[4] = {0,0,0,0};
            long long sumTot = 0;
            for (const auto &arr : clustersPerSpillVec) {
                sumD[0] += arr[0]; sumD[1] += arr[1]; sumD[2] += arr[2]; sumD[3] += arr[3];
            }
            sumTot = sumD[0] + sumD[1] + sumD[2] + sumD[3];
            double avgD0 = (nSpills>0) ? double(sumD[0]) / nSpills : 0.0;
            double avgD1 = (nSpills>0) ? double(sumD[1]) / nSpills : 0.0;
            double avgD2 = (nSpills>0) ? double(sumD[2]) / nSpills : 0.0;
            double avgD3 = (nSpills>0) ? double(sumD[3]) / nSpills : 0.0;
            double avgTot = (nSpills>0) ? double(sumTot) / nSpills : 0.0;
            y -= 0.02; // small gap
            // Split into two lines to avoid overflow
            lat.DrawLatex(0.12, y, Form("Average clusters per 10 s spill: total=%.1f  (over %lld spills)", avgTot, nSpills)); y -= dy;
            lat.DrawLatex(0.12, y, Form("Per detector: D0=%.1f, D1=%.1f, D2=%.1f, D3=%.1f", avgD0, avgD1, avgD2, avgD3)); y -= dy;
        }

        c_summary->Update();
        c_summary->SaveAs((output_filename_report + "(").c_str());

        // Move event displays to the beginning
        // Page 2: Reconstructed centers heatmap (exactly 3) with projections
        if (gROOT->FindObject("c_recoCenter_3")) {
            auto ctmp = (TCanvas*)gROOT->FindObject("c_recoCenter_3");
            ctmp->Update();
            ctmp->SaveAs(output_filename_report.c_str());
        }
        // Page 3: Reconstructed centers heatmap (2 or 3) with projections
        if (gROOT->FindObject("c_recoCenter_2to3")) {
            auto ctmp = (TCanvas*)gROOT->FindObject("c_recoCenter_2to3");
            ctmp->Update();
            ctmp->SaveAs(output_filename_report.c_str());
        }
        // Page 4: Reconstructed centers scatter-only (exactly 3) with %
        if (c_recoCenterScatter_3) {
            c_recoCenterScatter_3->cd();
            if (g_recoCenters_3->GetN()==0) { TLatex l; l.SetNDC(true); l.DrawLatex(0.3,0.5,"No points"); }
            c_recoCenterScatter_3->Update();
            c_recoCenterScatter_3->SaveAs(output_filename_report.c_str());
        }
        // Page 5: Reconstructed centers scatter-only (2 or 3) with %
        if (c_recoCenterScatter_2to3) {
            c_recoCenterScatter_2to3->cd();
            if (g_recoCenters_2to3->GetN()==0) { TLatex l; l.SetNDC(true); l.DrawLatex(0.3,0.5,"No points"); }
            c_recoCenterScatter_2to3->Update();
            c_recoCenterScatter_2to3->SaveAs(output_filename_report.c_str());
        }

    // Page 6: Clusters per event (per detector)
        {
            TCanvas *c_clustersPerEvent = new TCanvas(Form("c_clustersPerEvent_Run%s", runNumber.c_str()), Form("Clusters per event (Run %s)", runNumber.c_str()), 800, 600);
            c_clustersPerEvent->Divide(2, 2);
            for (int d = 0; d < 4; ++d) {
                c_clustersPerEvent->cd(d+1);
                gPad->SetLogy();
                // Update titles with run number at draw time for clarity
                h_clustersPerEvent[d]->SetTitle(Form("Clusters per event - D%d (Run %s);Clusters;Events", d, runNumber.c_str()));
                h_clustersPerEvent[d]->Draw();
            }
            c_clustersPerEvent->Update();
            c_clustersPerEvent->SaveAs(output_filename_report.c_str());
        }

    // Page 7+: The rest of the plots
        c_channelsFiring->Update();
        c_channelsFiring->SaveAs(output_filename_report.c_str());

        c_sigma->Update();
        c_sigma->SaveAs(output_filename_report.c_str());

        c_baseline->Update();
        c_baseline->SaveAs(output_filename_report.c_str());

        c_amplitude->Update();
        c_amplitude->SaveAs(output_filename_report.c_str());

    // Clean sample scatter pages removed per request

    // Timestamp plots
        if (c_evtVsTime != nullptr) { c_evtVsTime->Update(); c_evtVsTime->SaveAs(output_filename_report.c_str()); }
        if (c_extEvtVsTime != nullptr) { c_extEvtVsTime->Update(); c_extEvtVsTime->SaveAs(output_filename_report.c_str()); }
        if (c_evtDeltaT != nullptr) { c_evtDeltaT->Update(); c_evtDeltaT->SaveAs(output_filename_report.c_str()); }
        if (c_spillGaps != nullptr) { c_spillGaps->Update(); c_spillGaps->SaveAs(output_filename_report.c_str()); }

        // Final page: Hits in event plot (close the PDF)
        c_hitsInEvent->Update();
        c_hitsInEvent->SaveAs((output_filename_report + ")").c_str());
        
        LogInfo << "Multi-page PDF report saved successfully: " << output_filename_report << std::endl;
    } catch (const std::exception& e) {
        LogError << "Error saving PDF report: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        LogError << "Unknown error occurred while saving PDF report" << std::endl;
        return 1;
    }

    // Check if showPlots is enabled in JSON settings
    bool showPlots = parseBool(jsonSettings, "showPlots", false);
    LogInfo << "Show plots: " << (showPlots ? "true" : "false") << std::endl;

    if (showPlots) {
        LogInfo << "Running root viewer..." << std::endl;
        app->Run();
    }
    else { LogInfo << "If you wish to see the plots, you need to set showPlots option to true.\n";}

    return 0;
}
