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
#include "Math/Factory.h"
#include "Math/Minimizer.h"
#include "Math/Functor.h"
#include "Math/DistFunc.h"
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

// A more robust way to get the base name, removing known suffixes
std::string GetBaseName(std::string const & path) {
    std::string base = path.substr(path.find_last_of("/\\") + 1);
    static const std::vector<std::string> suffixes = {"_converted.root", ".root"};
    for (const auto& suffix : suffixes) {
        if (base.size() > suffix.size() && base.substr(base.size() - suffix.size()) == suffix) {
            return base.substr(0, base.size() - suffix.size());
        }
    }
    return base;
}

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
    // Accept new, legacy, and hybrid combinations per detector.
    const char* newTreeNames[4]   = {"detector_0", "detector_1", "detector_2", "detector_3"};
    const char* legacyTreeNames[4]= {"raw_events", "raw_events_B", "raw_events_C", "raw_events_D"};
    const char* newBranchName = "RAW Event";
    const char* legacyBranchNames[4] = {"RAW Event", "RAW Event B", "RAW Event C", "RAW Event D"};
    std::vector<TTree*> raw_events_trees; raw_events_trees.reserve(nDetectors);
    const char* branchNames[4] = {nullptr,nullptr,nullptr,nullptr};
    const char* treeNames[4]   = {nullptr,nullptr,nullptr,nullptr};
    auto tryBind = [&](int i)->bool{
        // 1) New tree + new branch
        if (TTree* t = (TTree*)input_root_file->Get(newTreeNames[i])) {
            if (t->GetBranch(newBranchName)) { raw_events_trees.emplace_back(t); branchNames[i]=newBranchName; treeNames[i]=newTreeNames[i]; return true; }
            // 2) New tree + legacy branch
            if (t->GetBranch(legacyBranchNames[i])) { raw_events_trees.emplace_back(t); branchNames[i]=legacyBranchNames[i]; treeNames[i]=newTreeNames[i]; return true; }
        }
        // 3) Legacy tree + new branch
        if (TTree* t = (TTree*)input_root_file->Get(legacyTreeNames[i])) {
            if (t->GetBranch(newBranchName)) { raw_events_trees.emplace_back(t); branchNames[i]=newBranchName; treeNames[i]=legacyTreeNames[i]; return true; }
            // 4) Legacy tree + legacy branch
            if (t->GetBranch(legacyBranchNames[i])) { raw_events_trees.emplace_back(t); branchNames[i]=legacyBranchNames[i]; treeNames[i]=legacyTreeNames[i]; return true; }
        }
        return false;
    };
    bool okAll = true; raw_events_trees.clear();
    for (int i=0;i<nDetectors;i++) {
        if (!tryBind(i)) { okAll=false; break; }
    }
    if (!okAll) {
        LogError << "Could not bind all detector trees/branches (new/legacy)." << std::endl;
        return 1;
    }
    LogInfo << "Bound detector TTrees/branches (new/legacy/hybrid accepted)." << std::endl;

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
    int maxEvents = 1000000000; // default value (no cap by default)
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
    int eventsTwoDetMultiCluster = 0; // events where at least two detectors among D0..D2 have >1 cluster
    int eventsThreeDetMultiCluster = 0; // events where all three detectors among D0..D2 have >1 cluster
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
    // Half-extent along strip-normal (active range)
    const double Umax = 0.5 * nChannels * channelPitch; // ~50 mm
    // Half-length along global X (detector is 10 cm long along X)
    const double halfX = 50.0; // mm
    
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
    double centersYmin = -60.0, centersYmax = 70.0; // mm (increase y max to 70)
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
    // Enforce X/Y ranges regardless of geometry file per request
    centersXmin = -80.0;     // mm
    centersXmax = 60.0;      // set X max to 60 mm
    centersYmax = 70.0;      // ensure Y max is 70 mm

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
    // Track second-order moments for covariance (for 1-sigma ellipse)
    double sumXX_2to3 = 0.0, sumYY_2to3 = 0.0, sumXY_2to3 = 0.0;
    double sumXX_3 = 0.0,    sumYY_3 = 0.0,    sumXY_3 = 0.0;

    // Pre-compute the convex polygon of the intersection of the three active planes
    auto computeActiveAreaPolygon = [&](bool flipYCoords){
        struct Line { double A,B,C; }; // A x + B y = C
        std::vector<Line> lines; lines.reserve(6);
        for (int i=0;i<geomDetN;i++) {
            double c = std::cos(normalAnglesRad[i]);
            double s = std::sin(normalAnglesRad[i]);
            double off = planeOffsets[i].first * c + planeOffsets[i].second * s;
            // +U boundary:  c x + s y = Umax + off
            lines.push_back({ c,  s,  Umax + off});
            // -U boundary: -c x - s y = Umax - off
            lines.push_back({-c, -s,  Umax - off});
            // Add physical X boundaries for each detector: x = x0 +/- halfX
            double x0 = planeOffsets[i].first;
            lines.push_back({ 1.0, 0.0, x0 + halfX});   //  x = x0+halfX
            lines.push_back({-1.0, 0.0, -(x0 - halfX)}); // -x = -(x0-halfX) => x = x0-halfX
        }
        auto satisfiesAll = [&](double x, double y){
            // Check |(x-x0)c + (y-y0)s| <= Umax for all planes
            for (int i=0;i<geomDetN;i++) {
                double c = std::cos(normalAnglesRad[i]);
                double s = std::sin(normalAnglesRad[i]);
                double u = (x - planeOffsets[i].first) * c + (y - planeOffsets[i].second) * s;
                if (std::fabs(u) > Umax + 1e-6) return false;
                // Check physical X extent for each plane (10 cm along global X)
                if (std::fabs(x - planeOffsets[i].first) > (halfX + 1e-6)) return false;
            }
            return true;
        };
        std::vector<std::pair<double,double>> pts;
        // Intersect all pairs of boundary lines
        for (size_t i=0;i<lines.size();++i){
            for (size_t j=i+1;j<lines.size();++j){
                double A1=lines[i].A, B1=lines[i].B, C1=lines[i].C;
                double A2=lines[j].A, B2=lines[j].B, C2=lines[j].C;
                double det = A1*B2 - A2*B1;
                if (std::fabs(det) < 1e-10) continue; // parallel
                double x = (C1*B2 - C2*B1)/det;
                double y = (A1*C2 - A2*C1)/det;
                if (satisfiesAll(x,y)) {
                    pts.emplace_back(x, flipYCoords ? -y : y);
                }
            }
        }
        // Deduplicate close points
        auto eqPt = [](const std::pair<double,double>& a, const std::pair<double,double>& b){
            return (std::fabs(a.first-b.first) < 1e-4) && (std::fabs(a.second-b.second) < 1e-4);
        };
        std::vector<std::pair<double,double>> uniq;
        for (auto &p: pts){ bool found=false; for (auto &q: uniq){ if (eqPt(p,q)){found=true;break;} } if (!found) uniq.push_back(p); }
        if (uniq.size() < 3) return uniq; // degenerate
        // Sort vertices counter-clockwise
        double cx=0, cy=0; for (auto &p: uniq){ cx+=p.first; cy+=p.second; } cx/=uniq.size(); cy/=uniq.size();
        std::sort(uniq.begin(), uniq.end(), [&](const auto& a, const auto& b){
            double aa = std::atan2(a.second - cy, a.first - cx);
            double bb = std::atan2(b.second - cy, b.first - cx);
            return aa < bb;
        });
        return uniq;
    };
    const auto activePolyPts = computeActiveAreaPolygon(flipY);

    // Build half-plane edges from the active polygon (interior on the left of each CCW edge)
    struct HalfPlane { double ax, ay, b, anorm; }; // inequality: a.x + a.y*y <= b, anorm = |a|
    auto buildHalfPlanes = [&](const std::vector<std::pair<double,double>>& poly){
        std::vector<HalfPlane> edges;
        if (poly.size() < 3) return edges;
        for (size_t i=0;i<poly.size();++i) {
            auto p = poly[i];
            auto q = poly[(i+1)%poly.size()];
            double dx = q.first - p.first;
            double dy = q.second - p.second;
            // Left normal for CCW polygon is n_left = (-dy, dx).
            // We want interior to satisfy a.x <= b, where a = (dy, -dx) (i.e., -n_left) and b = a.p
            double ax = dy, ay = -dx;
            double b  = ax * p.first + ay * p.second;
            double an = std::sqrt(ax*ax + ay*ay);
            // Sanity: ensure all vertices satisfy ax*x+ay*y <= b (if not, flip sign)
            bool ok = true;
            for (const auto &v : poly) { if (ax*v.first + ay*v.second > b + 1e-6) { ok=false; break; } }
            if (!ok) { ax = -ax; ay = -ay; b = -b; }
            edges.push_back({ax, ay, b, std::max(1e-12, std::hypot(ax, ay))});
        }
        return edges;
    };
    const auto activeEdges = buildHalfPlanes(activePolyPts);

    // Choose the nearest truncating edge to a reference point (e.g., sample mean)
    auto chooseNearestEdge = [&](double mx, double my)->HalfPlane{
        if (activeEdges.empty()) return {0,0,0,1};
        double bestD = 1e300; size_t bestI = 0;
        for (size_t i=0;i<activeEdges.size();++i) {
            const auto &e = activeEdges[i];
            double d = (e.b - (e.ax*mx + e.ay*my)) / e.anorm; // distance to line within interior
            if (d < bestD) { bestD = d; bestI = i; }
        }
        return activeEdges[bestI];
    };

    // Truncated half-plane bivariate normal MLE using Minuit2 (points, initial guess, edge)
    struct FitResult { bool ok=false; double mux=0, muy=0, sx=0, sy=0, rho=0; };
    auto fitTruncatedBvNormal = [&](const std::vector<std::pair<double,double>>& pts,
                                    double init_mx, double init_my,
                                    double init_sx, double init_sy,
                                    double init_rho,
                                    const HalfPlane &hp)->FitResult {
        FitResult res; if (pts.size() < 10) return res;
        // Negative log-likelihood
        auto nll = [&](const double *p)->double{
            double mx = p[0], my = p[1];
            double sx = std::max(1e-3, p[2]);
            double sy = std::max(1e-3, p[3]);
            double rho= std::max(-0.999, std::min(0.999, p[4]));
            double sxx = sx*sx, syy = sy*sy, sxy = rho*sx*sy;
            double det = sxx*syy - sxy*sxy;
            if (det <= 1e-12) return 1e30; // invalid covariance
            // Sigma^{-1}
            double inv00 =  syy / det;
            double inv11 =  sxx / det;
            double inv01 = -sxy / det;
            double logNorm = std::log(2*M_PI) + 0.5*std::log(det);
            // Truncation probability P = Phi(t)
            double a0 = hp.ax, a1 = hp.ay; double b = hp.b;
            double varAlong = a0*a0*sxx + 2*a0*a1*sxy + a1*a1*syy;
            if (varAlong <= 1e-18) return 1e29;
            double t = (b - (a0*mx + a1*my)) / std::sqrt(varAlong);
            double P = ROOT::Math::normal_cdf(t);
            if (P < 1e-12 || P>1.0) return 1e25; // outside support or numerical issues
            double ll = 0.0;
            for (const auto &pt : pts) {
                double dx = pt.first  - mx;
                double dy = pt.second - my;
                double q = dx*(inv00*dx + inv01*dy) + dy*(inv01*dx + inv11*dy);
                ll += -logNorm - 0.5*q;
            }
            // subtract normalization term for truncation
            ll += - (double)pts.size() * std::log(P);
            return -ll; // minimizer minimizes; we want to maximize ll
        };

        std::unique_ptr<ROOT::Math::Minimizer> min(ROOT::Math::Factory::CreateMinimizer("Minuit2", "Migrad"));
        if (!min) return res;
        min->SetMaxFunctionCalls(10000);
        min->SetMaxIterations(1000);
        min->SetTolerance(1e-4);
        ROOT::Math::Functor f([&](const double* p){ return nll(p); }, 5);
        min->SetFunction(f);
        // Parameters: mx,my,sx,sy,rho
        min->SetVariable(0, "mx",   init_mx, 0.05);
        min->SetVariable(1, "my",   init_my, 0.05);
        min->SetLimitedVariable(2, "sx", std::max(0.5, init_sx), 0.02, 0.1, 100.0);
        min->SetLimitedVariable(3, "sy", std::max(0.5, init_sy), 0.02, 0.1, 100.0);
        min->SetLimitedVariable(4, "rho", init_rho, 0.01, -0.95, 0.95);
        bool ok = min->Minimize();
        if (!ok) return res;
        const double *xs = min->X();
        res.ok  = true;
        res.mux = xs[0]; res.muy = xs[1]; res.sx = xs[2]; res.sy = xs[3]; res.rho = xs[4];
        return res;
    };

    // --- Preload timestamps from event_info to support 10s spill grouping ---
    // Use 'timestamp' ticks at 20 ns -> 10 s corresponds to 500,000,000 ticks
    const long long SPILL_TICKS = 500000000LL; // 10 s at 50 MHz (20 ns ticks)
    const double    TICK_PERIOD_SEC = 20e-9;   // seconds per internal timestamp tick
    const double    EXT_TICK_PERIOD_SEC = 64e-9; // seconds per EXTERNAL timestamp tick
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

            // Count multi-cluster cases: number of detectors with >1 cluster among D0..D2
            int multiDetCnt = (c0.first >= 2) + (c1.first >= 2) + (c2.first >= 2);
            if (multiDetCnt >= 2) eventsTwoDetMultiCluster++;
            if (multiDetCnt == 3) eventsThreeDetMultiCluster++;

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
                sumXX_2to3 += cx*cx; sumYY_2to3 += cy*cy; sumXY_2to3 += cx*cy;
                selected2to3Count++;
                // Additionally fill the exactly-3-clusters map when applicable
                if (acceptExactlyThree) {
                    h_recoCenter_3->Fill(cx, cy);
                    g_recoCenters_3->SetPoint(g_recoCenters_3->GetN(), cx, cy);
                    sumX_3 += cx; sumY_3 += cy; cnt_3++;
                    sumXX_3 += cx*cx; sumYY_3 += cy*cy; sumXY_3 += cx*cy;
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
    // Fill hits-per-event only for events with hits (>0), as requested
    if (eventHitsMasked > 0) {
        h_hitsInEvent->Fill(eventHitsMasked);
        eventsWithHits++;
    }
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

    // --- New: 4x4 overlay pages for baseline, sigma, and amplitude-vs-channel profiles ---
    // Common styles/colors for detectors
    int detColors[4] = { kBlue+1, kRed+1, kGreen+2, kMagenta+1 };
    int detMarkers[4] = { 20, 21, 22, 23 };
    const int chunks = 16; // 4x4 grid
    const int chunkSize = (nChannels + chunks - 1) / chunks; // ~24

    // Baseline: single overlay across full channel range
    TCanvas *c_baseline_all = new TCanvas(Form("c_baseline_all_Run%s", runNumber.c_str()), Form("Baseline (all channels) - Run %s", runNumber.c_str()), 900, 600);
    c_baseline_all->cd();
    {
        // Auto y-range from all detectors
        double yMin = 1e300, yMax = -1e300;
        for (int d=0; d<4; ++d) { for (int ch=0; ch<nChannels; ++ch) { double v = baseline.at(d).at(ch); if (v<yMin) yMin=v; if (v>yMax) yMax=v; } }
        if (!(yMax>yMin)) { yMin=0; yMax=1; }
        double pad = 0.05*(yMax-yMin+1e-6);
        TH1F *frame = new TH1F("frame_base_all", Form("Baseline vs channel - Run %s;Channel;Baseline", runNumber.c_str()), nChannels, 0, nChannels);
        frame->SetStats(0);
        frame->GetYaxis()->SetRangeUser(yMin-pad, yMax+pad);
        frame->Draw();
        auto leg = new TLegend(0.70, 0.78, 0.95, 0.94); leg->SetBorderSize(0); leg->SetFillStyle(0); leg->SetTextSize(0.03);
        for (int d=0; d<4; ++d) { auto gr=g_baseline->at(d); gr->SetMarkerStyle(detMarkers[d]); gr->SetMarkerSize(0.6); gr->SetMarkerColor(detColors[d]); gr->SetLineColor(detColors[d]); gr->Draw("P SAME"); leg->AddEntry(gr, Form("D%d", d), "lp"); }
        leg->Draw();
    }

    // Baseline 4x4 overlay removed per request

    // Sigma: single overlay across full channel range
    TCanvas *c_sigma_all = new TCanvas(Form("c_sigma_all_Run%s", runNumber.c_str()), Form("Sigma (all channels) - Run %s", runNumber.c_str()), 900, 600);
    c_sigma_all->cd();
    {
        double yMin = 1e300, yMax = -1e300;
        for (int d=0; d<4; ++d) { for (int ch=0; ch<nChannels; ++ch) { double v = baseline_sigma.at(d).at(ch); if (v<yMin) yMin=v; if (v>yMax) yMax=v; } }
        if (!(yMax>yMin)) { yMin=0; yMax=1; }
        double pad = 0.05*(yMax-yMin+1e-6);
        TH1F *frame = new TH1F("frame_sigma_all", Form("Sigma vs channel - Run %s;Channel;Sigma", runNumber.c_str()), nChannels, 0, nChannels);
        frame->SetStats(0);
        frame->GetYaxis()->SetRangeUser(yMin-pad, yMax+pad);
        frame->Draw();
        auto leg = new TLegend(0.70, 0.78, 0.95, 0.94); leg->SetBorderSize(0); leg->SetFillStyle(0); leg->SetTextSize(0.03);
        for (int d=0; d<4; ++d) { auto gr=g_sigma->at(d); gr->SetMarkerStyle(detMarkers[d]); gr->SetMarkerSize(0.6); gr->SetMarkerColor(detColors[d]); gr->SetLineColor(detColors[d]); gr->Draw("P SAME"); leg->AddEntry(gr, Form("D%d", d), "lp"); }
        leg->Draw();
    }

    // Sigma 4x4 overlay removed per request

    // Amplitude vs Channel: use mean amplitude per channel via TProfile and overlay
    std::vector<TProfile*> profAmp(4, nullptr);
    for (int d=0; d<4; ++d) {
        profAmp[d] = h_amplitudeVsChannel->at(d)->ProfileX(Form("p_amp_D%d", d));
        if (profAmp[d]) { profAmp[d]->SetLineColor(detColors[d]); profAmp[d]->SetMarkerColor(detColors[d]); profAmp[d]->SetMarkerStyle(detMarkers[d]); profAmp[d]->SetMarkerSize(0.6); profAmp[d]->SetLineWidth(2); }
    }
    // Amplitude: single overlay of mean amplitude per channel across full range
    TCanvas *c_ampl_all = new TCanvas(Form("c_amp_all_Run%s", runNumber.c_str()), Form("Mean amplitude (all channels) - Run %s", runNumber.c_str()), 900, 600);
    c_ampl_all->cd();
    {
        double yMin = 1e300, yMax = -1e300;
        for (int d=0; d<4; ++d) if (profAmp[d]) { for (int ch=1; ch<=nChannels; ++ch) { double v = profAmp[d]->GetBinContent(ch); if (v==0) continue; if (v<yMin) yMin=v; if (v>yMax) yMax=v; } }
        if (!(yMax>yMin)) { yMin=0; yMax=1; }
        double pad = 0.10*(yMax-yMin+1e-6);
        TH1F *frame = new TH1F("frame_amp_all", Form("Mean amplitude vs channel - Run %s;Channel;Mean amplitude", runNumber.c_str()), nChannels, 0, nChannels);
        frame->SetStats(0);
        frame->GetYaxis()->SetRangeUser(yMin-pad, yMax+pad);
        frame->Draw();
        auto leg = new TLegend(0.70, 0.78, 0.95, 0.94); leg->SetBorderSize(0); leg->SetFillStyle(0); leg->SetTextSize(0.03);
        for (int d=0; d<4; ++d) if (profAmp[d]) { profAmp[d]->Draw("P SAME"); leg->AddEntry(profAmp[d], Form("D%d", d), "lp"); }
        leg->Draw();
    }

    TCanvas *c_ampl_4x4 = new TCanvas(Form("c_amp4x4_Run%s", runNumber.c_str()), Form("Amplitude mean (4x4 slices) - Run %s", runNumber.c_str()), 1200, 900);
    c_ampl_4x4->Divide(4,4);
    for (int chIt=0; chIt<chunks; ++chIt) {
        int s = chIt * chunkSize;
        int e = std::min(nChannels, s + chunkSize);
        if (s >= e) break;
        c_ampl_4x4->cd(chIt+1);
        double yMin = 1e300, yMax = -1e300;
        for (int d=0; d<4; ++d) {
            if (!profAmp[d]) continue;
            for (int ch=s; ch<e; ++ch) {
                int bin = ch+1;
                double v = profAmp[d]->GetBinContent(bin);
                if (v == 0) continue; // ignore empty
                if (v < yMin) yMin = v; if (v > yMax) yMax = v;
            }
        }
        if (!(yMax > yMin)) { yMin = 0; yMax = 1; }
        double pad = 0.10 * (yMax - yMin + 1e-6);
        TH1F *frame = new TH1F(Form("frame_amp_%d", chIt), Form("Mean amplitude ch [%d,%d) - Run %s;Channel;Mean amplitude", s, e-1, runNumber.c_str()), e-s, s, e);
        frame->SetStats(0);
        frame->GetYaxis()->SetRangeUser(yMin - pad, yMax + pad);
        frame->Draw();
        for (int d=0; d<4; ++d) { if (profAmp[d]) profAmp[d]->Draw("P SAME"); }
        auto leg = new TLegend(0.60, 0.78, 0.95, 0.94);
        leg->SetBorderSize(0); leg->SetFillStyle(0); leg->SetTextSize(0.030);
        for (int d=0; d<4; ++d) if (profAmp[d]) leg->AddEntry(profAmp[d], Form("D%d", d), "lp");
        leg->Draw();
    }

    // --- New plots: time distances ---
    TH1F *h_evtDeltaT = nullptr; TCanvas *c_evtDeltaT = nullptr;
    if (!evtDeltaTimesSec.empty()) {
        double maxDt = *std::max_element(evtDeltaTimesSec.begin(), evtDeltaTimesSec.end());
        if (maxDt <= 0) maxDt = 1.0;
    int nbins = 200;
    h_evtDeltaT = new TH1F(Form("h_evtDeltaT_%s", runNumber.c_str()), Form("Delta between consecutive events (Run %s);#Delta t [s];Entries", runNumber.c_str()), nbins, 0.0, 0.5);
        for (double v : evtDeltaTimesSec) h_evtDeltaT->Fill(v);
        c_evtDeltaT = new TCanvas(Form("c_evtDeltaT_Run%s", runNumber.c_str()), Form("Event #Delta t (Run %s)", runNumber.c_str()), 800, 600);
        c_evtDeltaT->cd(); h_evtDeltaT->Draw(); c_evtDeltaT->Update();
    }
    // Requested: remove the "gap between 10s spills" plot entirely

    // Build timestamp vs trigger NUMBER graph, and external timestamp vs trigger, if available
    TGraph *g_evtVsTime = nullptr;
    TCanvas *c_evtVsTime = nullptr;
    double timeMin = 0, timeMax = 0; // minutes
    TGraph *g_extEvtVsTime = nullptr;
    TCanvas *c_extEvtVsTime = nullptr;
    double extTimeMin = 0, extTimeMax = 0; // minutes
    // New: external vs internal time
    TGraph *g_extVsIntTime = nullptr;
    TCanvas *c_extVsIntTime = nullptr;
    {
        // Parse start time from input_root_filename (add +2h for CEST) to embed in titles
    auto buildSinceLabel = [&](const std::string &fname)->std::string{
            std::string base = fname.substr(fname.find_last_of("/\\") + 1);
            size_t posTime = base.rfind('_');
            auto isAllDigits = [](const std::string &s){ return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit); };
            if (posTime != std::string::npos && posTime+1 < base.size()) {
                std::string timeTok = base.substr(posTime+1); // HHMMSS(.root)
                size_t dot = timeTok.find('.'); if (dot != std::string::npos) timeTok = timeTok.substr(0,dot);
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
                        hour += 2; if (hour >= 24) { hour -= 24; day += 1; int dim = daysInMonth(year,month); if (day>dim){ day=1; month+=1; if (month>12){ month=1; year+=1; } } }
                        return std::string(Form("since %04d-%02d-%02d %02d:%02d CEST", year, month, day, hour, minute));
                    }
                }
            }
            // Avoid generic 'start' per request; fallback to a generic CEST label
            return std::string("since unknown CEST");
        };
        std::string sinceLabel = buildSinceLabel(input_root_filename);
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
                // Time since start on X; concise title includes run number
                g_evtVsTime->SetTitle(Form("Time since beginning of run, internal timestamp - Run %s", runNumber.c_str()));
                g_evtVsTime->GetXaxis()->SetTitle("Internal time [min]");
                g_evtVsTime->GetYaxis()->SetTitle("Trigger Number");
                g_evtVsTime->SetMarkerStyle(20);
                g_evtVsTime->SetMarkerSize(0.6);
                Long64_t t0 = 0; bool t0set = false; timeMin = 0; timeMax = 0;
                for (Long64_t i = 0; i < nToRead; ++i) {
                    infoT->GetEntry(i);
                    if (!t0set) { t0 = ts; t0set = true; }
                    double dtTicks = double((ts >= t0) ? (ts - t0) : 0LL);
                    double dtMin = dtTicks * TICK_PERIOD_SEC / 60.0; // minutes
                    if (i == 0) { timeMin = dtMin; timeMax = dtMin; }
                    else { if (dtMin < timeMin) timeMin = dtMin; if (dtMin > timeMax) timeMax = dtMin; }
                    double xTrig = hasTrigNum ? double(trigNum) : double(trigId);
                    // Inverted: X=Time[min], Y=Trigger
                    g_evtVsTime->SetPoint(g_evtVsTime->GetN(), dtMin, xTrig);
                }
                c_evtVsTime = new TCanvas(Form("c_timeVsTriggerNumber_Run%s", runNumber.c_str()), Form("Timestamp vs Trigger Number (Run %s)", runNumber.c_str()), 800, 600);
                c_evtVsTime->cd();
                g_evtVsTime->Draw("AP");
                if (timeMax > timeMin) {
                    double pad = 0.01 * (timeMax - timeMin);
                    g_evtVsTime->GetXaxis()->SetLimits(timeMin - pad, timeMax + pad);
                }
                c_evtVsTime->Update();
                LogInfo << "Internal time span (min): [" << timeMin << ", " << timeMax << "]" << std::endl;
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
                // Time since start on X; concise title includes run number
                g_extEvtVsTime->SetTitle(Form("Time since beginning of run, external timestamp - Run %s", runNumber.c_str()));
                g_extEvtVsTime->GetXaxis()->SetTitle("External time [min]");
                g_extEvtVsTime->GetYaxis()->SetTitle("Trigger Number");
                g_extEvtVsTime->SetMarkerStyle(20);
                g_extEvtVsTime->SetMarkerSize(0.6);
                Long64_t ext0 = 0; bool ext0set = false; extTimeMin = 0; extTimeMax = 0;
                for (Long64_t i = 0; i < nToRead; ++i) {
                    infoT->GetEntry(i);
                    if (!ext0set) { ext0 = ext_ts; ext0set = true; }
                    double dtTicks = double((ext_ts >= ext0) ? (ext_ts - ext0) : 0LL);
                    double dtMin = dtTicks * EXT_TICK_PERIOD_SEC / 60.0; // minutes
                    if (i == 0) { extTimeMin = dtMin; extTimeMax = dtMin; }
                    else { if (dtMin < extTimeMin) extTimeMin = dtMin; if (dtMin > extTimeMax) extTimeMax = dtMin; }
                    double xTrig = hasTrigNum ? double(trigNum) : double(trigId);
                    // Inverted: X=External Time[min], Y=Trigger
                    g_extEvtVsTime->SetPoint(g_extEvtVsTime->GetN(), dtMin, xTrig);
                }
                c_extEvtVsTime = new TCanvas(Form("c_extTimeVsTriggerNumber_Run%s", runNumber.c_str()), Form("External timestamp vs Trigger Number (Run %s)", runNumber.c_str()), 800, 600);
                c_extEvtVsTime->cd();
                g_extEvtVsTime->Draw("AP");
                if (extTimeMax > extTimeMin) {
                    double pad = 0.01 * (extTimeMax - extTimeMin);
                    g_extEvtVsTime->GetXaxis()->SetLimits(extTimeMin - pad, extTimeMax + pad);
                }
                c_extEvtVsTime->Update();
                LogInfo << "External time span (min): [" << extTimeMin << ", " << extTimeMax << "]" << std::endl;
                LogInfo << "Built External timestamp vs Trigger Number graph from 'event_info' TTree (" << nToRead << " points)." << std::endl;
            }

            // New: External timestamp vs Internal timestamp (both in minutes since start)
            if (hasTs && hasExt) {
                Long64_t nEvt = infoT->GetEntries();
                Long64_t nToRead = std::min<Long64_t>(nEvt, limit);
                g_extVsIntTime = new TGraph();
                g_extVsIntTime->SetName("g_extVsIntTime");
                g_extVsIntTime->SetTitle(Form("Internal vs external timestamps (in ticks) - Run %s;Internal time [min];External time [min]", runNumber.c_str()));
                g_extVsIntTime->SetMarkerStyle(20);
                g_extVsIntTime->SetMarkerSize(0.6);
                Long64_t t0 = 0, ext0 = 0; bool t0set = false, ext0set = false;
                double xmin = 0, xmax = 0, ymin = 0, ymax = 0;
                for (Long64_t i = 0; i < nToRead; ++i) {
                    infoT->GetEntry(i);
                    if (!t0set) { t0 = ts; t0set = true; }
                    if (!ext0set) { ext0 = ext_ts; ext0set = true; }
                    double dtIntTicks = double((ts >= t0) ? (ts - t0) : 0LL);
                    double dtExtTicks = double((ext_ts >= ext0) ? (ext_ts - ext0) : 0LL);
                    double xMin = dtIntTicks * TICK_PERIOD_SEC / 60.0; // minutes
                    double yMin = dtExtTicks * EXT_TICK_PERIOD_SEC / 60.0; // minutes
                    if (i == 0) { xmin = xmax = xMin; ymin = ymax = yMin; }
                    else {
                        if (xMin < xmin) xmin = xMin; if (xMin > xmax) xmax = xMin;
                        if (yMin < ymin) ymin = yMin; if (yMin > ymax) ymax = yMin;
                    }
                    g_extVsIntTime->SetPoint(g_extVsIntTime->GetN(), xMin, yMin);
                }
                c_extVsIntTime = new TCanvas(Form("c_extVsIntTime_Run%s", runNumber.c_str()), Form("External vs Internal time since start (Run %s)", runNumber.c_str()), 800, 600);
                c_extVsIntTime->cd();
                g_extVsIntTime->Draw("AP");
                // Set sensible axis ranges
                if (xmax > xmin) {
                    double padx = 0.01 * (xmax - xmin);
                    g_extVsIntTime->GetXaxis()->SetLimits(xmin - padx, xmax + padx);
                }
                if (ymax > ymin) {
                    double pady = 0.01 * (ymax - ymin);
                    g_extVsIntTime->GetYaxis()->SetRangeUser(ymin - pady, ymax + pady);
                }
                c_extVsIntTime->Update();
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

    // Helper to map user coordinates to NDC inside a pad (for stat box placement)
    auto userToNDC = [&](TPad* pad, double ux, double uy){
        double uxmin = pad->GetUxmin();
        double uxmax = pad->GetUxmax();
        double uymin = pad->GetUymin();
        double uymax = pad->GetUymax();
        double xndc = pad->GetLeftMargin() + (ux-uxmin)/(uxmax-uxmin) * (1.0 - pad->GetLeftMargin() - pad->GetRightMargin());
        double yndc = pad->GetBottomMargin() + (uy-uymin)/(uymax-uymin) * (1.0 - pad->GetTopMargin() - pad->GetBottomMargin());
        return std::pair<double,double>(xndc, yndc);
    };

    // Update 2D heatmap titles with percentages and prepare canvases with projections
    {
    double pTrig3 = (triggeredEvents > 0) ? (100.0 * (double)selected3Count / (double)triggeredEvents) : 0.0;
    double pTot3  = (limit > 0) ? (100.0 * (double)selected3Count / (double)limit) : 0.0;
    std::string title3 = Form("Tracking (1 cluster/Det) [%.1f%% of events with hits, %.1f%% of all events] - Run %s;",
                   pTrig3, pTot3, runNumber.c_str());
        h_recoCenter_3->SetTitle(title3.c_str());

    // Build canvas with main pad + bottom (ProjectionX) + left (ProjectionY)
    TCanvas *c = new TCanvas("c_recoCenter_3", Form("Exactly 3 (Run %s)", runNumber.c_str()), 980, 820);
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
        // Overlay active area polygon (intersection of three planes)
        if (activePolyPts.size() >= 3) {
            auto gpoly = new TGraph((int)activePolyPts.size()+1);
            for (int ip=0; ip<(int)activePolyPts.size(); ++ip) gpoly->SetPoint(ip, activePolyPts[ip].first, activePolyPts[ip].second);
            gpoly->SetPoint((int)activePolyPts.size(), activePolyPts[0].first, activePolyPts[0].second);
            gpoly->SetLineColor(kBlack);
            gpoly->SetLineWidth(2);
            gpoly->SetFillStyle(0);
            gpoly->Draw("L SAME");
            // Legend moved a bit lower, within the plot
            auto leg = new TLegend(0.30, 0.68, 0.58, 0.82);
            leg->SetBorderSize(0);
            leg->SetFillStyle(0);
            leg->SetTextSize(0.028);
            leg->AddEntry(gpoly, "Active area with all 3 detectors", "l");
            leg->Draw();
        }
        // Place stats box within x=[30,60] mm and high in Y avoiding palette/right margin
        padMain3->Modified(); padMain3->Update();
        if (auto st = dynamic_cast<TPaveStats*>(h_recoCenter_3->GetListOfFunctions()->FindObject("stats"))) {
            double x1u = 30.0, x2u = 60.0; // mm
            double y2u = centersYmax - 5.0; // slightly below top
            double y1u = y2u - 15.0;        // base box height in user units (before scaling)
            auto p1 = userToNDC(padMain3, x1u, y1u);
            auto p2 = userToNDC(padMain3, x2u, y2u);
            // Ensure we don't encroach into the right margin/palette
            double eps = 0.01;
            double rightLimit = 1.0 - padMain3->GetRightMargin() - eps;
            if (p2.first > rightLimit) { double shift = p2.first - rightLimit; p1.first -= shift; p2.first -= shift; }
            // Desired vertical scale: 3x current height
            double topLimit = 1.0 - padMain3->GetTopMargin() - eps;
            double bottomPad = padMain3->GetBottomMargin();
            double availHeight = topLimit - bottomPad;
            double baseH = p2.second - p1.second;
            double desiredH = 3.0 * baseH;
            // Clamp desired height to available space
            if (desiredH > 0.95 * availHeight) desiredH = 0.95 * availHeight;
            // Keep the top anchored near y2 and expand downward
            if (p2.second > topLimit) p2.second = topLimit;
            double newY1 = p2.second - desiredH;
            // Keep above ~70% height to avoid polygon region
            double minY = padMain3->GetBottomMargin() + 0.70*(1.0 - padMain3->GetTopMargin() - padMain3->GetBottomMargin());
            if (newY1 < minY) newY1 = minY;
            // Ensure we still fit under the top limit
            double newY2 = newY1 + desiredH;
            if (newY2 > topLimit) { double dy = newY2 - topLimit; newY1 -= dy; newY2 -= dy; }
            // Apply
            st->SetX1NDC(p1.first); st->SetX2NDC(p2.first);
            st->SetY1NDC(newY1);    st->SetY2NDC(newY2);
        }
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
    std::string title23 = Form("Tracking (2/3 clusters, 1/detector) [%.1f%% of events with hits, %.1f%% of all events] - Run %s;X [mm];Y [mm]",
                    pTrig23, pTot23, runNumber.c_str());
        h_recoCenter_2to3->SetTitle(title23.c_str());

    TCanvas *c = new TCanvas("c_recoCenter_2to3", Form("2 or 3 (Run %s)", runNumber.c_str()), 980, 820);
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
        // Overlay active area polygon and 1-sigma ellipse (2-3 sample)
        if (activePolyPts.size() >= 3) {
            auto gpoly = new TGraph((int)activePolyPts.size()+1);
            for (int ip=0; ip<(int)activePolyPts.size(); ++ip) gpoly->SetPoint(ip, activePolyPts[ip].first, activePolyPts[ip].second);
            gpoly->SetPoint((int)activePolyPts.size(), activePolyPts[0].first, activePolyPts[0].second);
            gpoly->SetLineColor(kBlack);
            gpoly->SetLineWidth(2);
            gpoly->SetFillStyle(0);
            gpoly->Draw("L SAME");
            auto leg = new TLegend(0.30, 0.68, 0.58, 0.82);
            leg->SetBorderSize(0);
            leg->SetFillStyle(0);
            leg->SetTextSize(0.028);
            leg->AddEntry(gpoly, "Active area with all 3 detectors", "l");
            leg->Draw();
        }
        // Reposition stats box similarly with mapping from user to NDC
        padMain23->Modified(); padMain23->Update();
        if (auto st = dynamic_cast<TPaveStats*>(h_recoCenter_2to3->GetListOfFunctions()->FindObject("stats"))) {
            double x1u = 30.0, x2u = 60.0; // mm
            double y2u = centersYmax - 5.0; // slightly below top
            double y1u = y2u - 15.0;        // base box height in user units (before scaling)
            auto p1 = userToNDC(padMain23, x1u, y1u);
            auto p2 = userToNDC(padMain23, x2u, y2u);
            double eps = 0.01;
            double rightLimit = 1.0 - padMain23->GetRightMargin() - eps;
            if (p2.first > rightLimit) { double shift = p2.first - rightLimit; p1.first -= shift; p2.first -= shift; }
            // Desired vertical scale: 3x current height
            double topLimit = 1.0 - padMain23->GetTopMargin() - eps;
            double bottomPad = padMain23->GetBottomMargin();
            double availHeight = topLimit - bottomPad;
            double baseH = p2.second - p1.second;
            double desiredH = 3.0 * baseH;
            if (desiredH > 0.95 * availHeight) desiredH = 0.95 * availHeight;
            if (p2.second > topLimit) p2.second = topLimit;
            double newY1 = p2.second - desiredH;
            double minY = padMain23->GetBottomMargin() + 0.70*(1.0 - padMain23->GetTopMargin() - padMain23->GetBottomMargin());
            if (newY1 < minY) newY1 = minY;
            double newY2 = newY1 + desiredH;
            if (newY2 > topLimit) { double dy = newY2 - topLimit; newY1 -= dy; newY2 -= dy; }
            st->SetX1NDC(p1.first); st->SetX2NDC(p2.first);
            st->SetY1NDC(newY1);    st->SetY2NDC(newY2);
        }
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
    std::string ttl3 = Form("Clusters (3 detectors) [%.1f%% of events with hits, %.1f%% of all events] - Run %s;X [mm];Y [mm]",
                 pTrig3, pTot3, runNumber.c_str());
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
    std::string ttl23 = Form("Clusters (2 or 3 detectors) [%.1f%% of events with hits, %.1f%% of all events] - Run %s;X [mm];Y [mm]",
                 pTrig23, pTot23, runNumber.c_str());
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
    // Requested: linear scale for number of clusters/hits per event
    gPad->SetLogy(0);
    h_hitsInEvent->Draw();

    // create a pdf report containing firing channels, sigma and amplitude
    std::string outputDir = clp.getOptionVal<std::string>("outputDir");
    // LogInfo << "Output directory: " << outputDir << std::endl;
    // output filename same as input root file, but remove .root and create single PDF report

    std::string input_file_base = GetBaseName(input_root_filename);
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
        // Check that essential canvases are valid before attempting to save
        if (!c_channelsFiring || !c_hitsInEvent) {
            LogError << "One or more essential canvases are null, cannot save plots" << std::endl;
            return 1;
        }
        
        LogInfo << "Creating multi-page PDF report..." << std::endl;
        // Page 1: Summary page with general run information
        TCanvas *c_summary = new TCanvas(Form("c_summary_Run%s", runNumber.c_str()), Form("Run %s Summary", runNumber.c_str()), 800, 600);
        c_summary->cd();
    TLatex lat;
    lat.SetNDC(true);
    // Make text smaller as requested and leave ample spacing
    lat.SetTextSize(0.030);
    double yTop = 0.93; const double dyHead = 0.05;
    // Add page number in top-right corner
    lat.SetTextSize(0.025);
    lat.SetTextAlign(31); // right-aligned
    lat.DrawLatex(0.95, 0.03, "Page 1");
    lat.SetTextAlign(11); // back to left-aligned
    lat.SetTextSize(0.030);
    // Header: include CEST date/time (+2h) parsed from file name
        std::string cestHeader = "";
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
                        hour += 2; if (hour >= 24) { hour -= 24; day += 1; int dim = daysInMonth(year,month); if (day>dim){ day=1; month+=1; if (month>12){ month=1; year+=1; } } }
                        cestHeader = std::string(Form("Run %s summary - Started %04d-%02d-%02d %02d:%02d CEST", runNumber.c_str(), year, month, day, hour, minute));
                    }
                }
            }
        }
    if (cestHeader.empty()) cestHeader = Form("Run %s summary", runNumber.c_str());
    lat.DrawLatex(0.10, yTop, cestHeader.c_str()); yTop -= dyHead;
    // Add date and time in CEST right after the title
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
                // lat.DrawLatex(0.10, yTop, Form("Date: %04d-%02d-%02d %02d:%02d CEST", year, month, day, hour, minute)); yTop -= 0.04;
            }
        }
    }
    // Two side-by-side mini tables for readability (non-overlapping)
    // Increased spacing for better readability
    const double rowH = 0.035; const double headH = 0.040; // Increased vertical spacing
    // Left table (x columns) - more horizontal spacing
    double xL = 0.04, xL_num = xL + 0.35, xL_pct = xL + 0.50; // Increased horizontal spacing
    double yL = yTop - 0.03; // Start tables with more space below header
    lat.DrawLatex(xL_num, yL, "number"); lat.DrawLatex(xL_pct, yL, "percentage"); yL -= rowH;
    lat.DrawLatex(xL, yL, "Total triggers"); lat.DrawLatex(xL_num, yL, Form("%d", limit)); lat.DrawLatex(xL_pct, yL, "100.00%" ); yL -= rowH;
    lat.DrawLatex(xL, yL, "Triggers with BM hits"); lat.DrawLatex(xL_num, yL, Form("%d", eventsWithHits)); lat.DrawLatex(xL_pct, yL, Form("%.2f%%", (limit>0)?(100.0*eventsWithHits/limit):0.0)); yL -= rowH;
    lat.DrawLatex(xL, yL, ">=1 cluster in >=2 detectors"); lat.DrawLatex(xL_num, yL, Form("%d", eventsTwoDetOrMore)); lat.DrawLatex(xL_pct, yL, Form("%.2f%%", (limit>0)?(100.0*eventsTwoDetOrMore/limit):0.0)); yL -= rowH;
    lat.DrawLatex(xL, yL, "1 cluster in 2 or 3 detectors"); lat.DrawLatex(xL_num, yL, Form("%d", triggeredEvents)); lat.DrawLatex(xL_pct, yL, Form("%.2f%%", (limit>0)?(100.0*triggeredEvents/limit):0.0));
    // Right table: extend with average clusters per spill and per event
    double xR = 0.04, xR_spill = xR + 0.35, xR_event = xR + 0.55; // 3 columns: label | per spill | per event
    double yR = yL - 0.10; // Increased vertical spacing before second table
    // header row
    lat.DrawLatex(xR, yR, "Average clusters"); lat.DrawLatex(xR_spill, yR, "per spill"); lat.DrawLatex(xR_event, yR, "per event"); yR -= rowH;
    // compute averages once
    double avgSpillD[4] = {0,0,0,0}; long long nSpills = (long long)clustersPerSpillVec.size();
    if (!clustersPerSpillVec.empty()) {
        long long sumD[4] = {0,0,0,0};
        for (const auto &arr : clustersPerSpillVec) { sumD[0]+=arr[0]; sumD[1]+=arr[1]; sumD[2]+=arr[2]; sumD[3]+=arr[3]; }
        for (int d=0; d<4; ++d) avgSpillD[d] = (nSpills>0) ? double(sumD[d])/nSpills : 0.0;
    }
    // per-event averages from already computed h_clustersPerEvent histos
    double avgEventD[4] = {0,0,0,0};
    for (int d=0; d<4; ++d) {
        if (h_clustersPerEvent[d]) avgEventD[d] = h_clustersPerEvent[d]->GetMean();
    }
    for (int d=0; d<4; ++d) {
        lat.DrawLatex(xR, yR, Form("D%d:", d));
        lat.DrawLatex(xR_spill, yR, Form("%.1f", avgSpillD[d]));
        lat.DrawLatex(xR_event, yR, Form("%.2f", avgEventD[d]));
        yR -= rowH;
    }
    if (nSpills>0) { lat.DrawLatex(xR, yR, Form("(over %lld spills)", nSpills)); }
    // Move below tables for remaining text with adequate spacing
    double y = std::min(yL, yR) - 0.10;
    // Removed geometry path line per request
    if (cnt_2to3>0) { lat.DrawLatex(0.05, y, Form("Mean center (2or3): (%.2f, %.2f) mm", sumX_2to3/cnt_2to3, sumY_2to3/cnt_2to3)); y -= 0.035; }
    if (cnt_3>0)    { lat.DrawLatex(0.05, y, Form("Mean center (exactly3): (%.2f, %.2f) mm", sumX_3/cnt_3, sumY_3/cnt_3)); y -= 0.035; }
    // Spill averages already listed in the table above

        c_summary->Update();
        c_summary->SaveAs((output_filename_report + "(").c_str());

        // Move event displays to the beginning
        // Page 2: Reconstructed centers heatmap (exactly 3) with projections
        if (gROOT->FindObject("c_recoCenter_3")) {
            auto ctmp = (TCanvas*)gROOT->FindObject("c_recoCenter_3");
            ctmp->cd();
            TLatex pageNum; pageNum.SetNDC(true); pageNum.SetTextSize(0.025); pageNum.SetTextAlign(31);
            pageNum.DrawLatex(0.95, 0.03, "Page 2");
            ctmp->Update();
            ctmp->SaveAs(output_filename_report.c_str());
        }
        // Page 3: Reconstructed centers heatmap (2 or 3) with projections
        if (gROOT->FindObject("c_recoCenter_2to3")) {
            auto ctmp = (TCanvas*)gROOT->FindObject("c_recoCenter_2to3");
            ctmp->cd();
            TLatex pageNum; pageNum.SetNDC(true); pageNum.SetTextSize(0.025); pageNum.SetTextAlign(31);
            pageNum.DrawLatex(0.95, 0.03, "Page 3");
            ctmp->Update();
            ctmp->SaveAs(output_filename_report.c_str());
        }
        // Page 4: Reconstructed centers scatter-only (exactly 3) with %
        if (c_recoCenterScatter_3) {
            c_recoCenterScatter_3->cd();
            if (g_recoCenters_3->GetN()==0) { TLatex l; l.SetNDC(true); l.DrawLatex(0.3,0.5,"No points"); }
            TLatex pageNum; pageNum.SetNDC(true); pageNum.SetTextSize(0.025); pageNum.SetTextAlign(31);
            pageNum.DrawLatex(0.95, 0.03, "Page 4");
            c_recoCenterScatter_3->Update();
            c_recoCenterScatter_3->SaveAs(output_filename_report.c_str());
        }
        // Page 5: Reconstructed centers scatter-only (2 or 3) with %
        if (c_recoCenterScatter_2to3) {
            c_recoCenterScatter_2to3->cd();
            if (g_recoCenters_2to3->GetN()==0) { TLatex l; l.SetNDC(true); l.DrawLatex(0.3,0.5,"No points"); }
            TLatex pageNum; pageNum.SetNDC(true); pageNum.SetTextSize(0.025); pageNum.SetTextAlign(31);
            pageNum.DrawLatex(0.95, 0.03, "Page 5");
            c_recoCenterScatter_2to3->Update();
            c_recoCenterScatter_2to3->SaveAs(output_filename_report.c_str());
        }

        // Page 6: Clusters per event (per detector)
        {
            TCanvas *c_clustersPerEvent = new TCanvas(Form("c_clustersPerEvent_Run%s", runNumber.c_str()), Form("Clusters per event (Run %s)", runNumber.c_str()), 800, 600);
            c_clustersPerEvent->Divide(2, 2);
            // Add page number
            c_clustersPerEvent->cd();
            TLatex pageNum; pageNum.SetNDC(true); pageNum.SetTextSize(0.025); pageNum.SetTextAlign(31);
            pageNum.DrawLatex(0.95, 0.03, "Page 6");
            for (int d = 0; d < 4; ++d) {
                c_clustersPerEvent->cd(d+1);
                // Linear scale as requested
                gPad->SetLogy(0);
                // Update titles with run number at draw time for clarity
                h_clustersPerEvent[d]->SetTitle(Form("Clusters per event - D%d (Run %s);Clusters;Events", d, runNumber.c_str()));
                // Limit x-axis to maximum 6 clusters
                h_clustersPerEvent[d]->GetXaxis()->SetRangeUser(0, 6);
                // Leave some headroom for labels
                double maxVal = h_clustersPerEvent[d]->GetMaximum();
                if (maxVal > 0) h_clustersPerEvent[d]->SetMaximum(maxVal * 1.25);
                h_clustersPerEvent[d]->Draw();
                // Add % above each bin
                double totEv = (double)limit;
                int nb = h_clustersPerEvent[d]->GetNbinsX();
                TLatex lab; lab.SetTextSize(0.035); lab.SetTextAlign(21);
                for (int b = 1; b <= nb; ++b) {
                    double val = h_clustersPerEvent[d]->GetBinContent(b);
                    if (val <= 0) continue;
                    double x = h_clustersPerEvent[d]->GetBinCenter(b);
                    double y = val * 1.02; // a bit above the bar
                    double pct = (totEv>0.0) ? (100.0 * val / totEv) : 0.0;
                    lab.DrawLatex(x, y, Form("%.1f%%", pct));
                }
            }
            c_clustersPerEvent->Update();
            c_clustersPerEvent->SaveAs(output_filename_report.c_str());
        }

        // Page 7: Timestamp plots arranged on a 2x2 page
        {
            TCanvas *c_time2x2 = new TCanvas(Form("c_time2x2_Run%s", runNumber.c_str()), Form("Timestamps - Run %s", runNumber.c_str()), 1200, 900);
            c_time2x2->Divide(2,2);
            // Add page number
            c_time2x2->cd();
            TLatex pageNum; pageNum.SetNDC(true); pageNum.SetTextSize(0.025); pageNum.SetTextAlign(31);
            pageNum.DrawLatex(0.95, 0.03, "Page 7");
            int padIdx = 1;
            if (g_evtVsTime) { c_time2x2->cd(padIdx++); g_evtVsTime->Draw("AP"); }
            if (g_extEvtVsTime) { c_time2x2->cd(padIdx++); g_extEvtVsTime->Draw("AP"); }
            if (g_extVsIntTime) { c_time2x2->cd(padIdx++); g_extVsIntTime->Draw("AP"); }
            // Pad (2,2): Delta time between consecutive events with exponential fit annotation
            if (h_evtDeltaT) {
                c_time2x2->cd(4);
                h_evtDeltaT->Draw();
                // Apply exponential fit: f(t) = A * exp(-t/tau) => 'expo' uses exp([0] + [1]*x), tau = -1/[1]
                int firstNonZeroBin = 1; while (firstNonZeroBin <= h_evtDeltaT->GetNbinsX() && h_evtDeltaT->GetBinContent(firstNonZeroBin) <= 0) ++firstNonZeroBin;
                double fitMin = h_evtDeltaT->GetXaxis()->GetBinLowEdge(std::max(1, firstNonZeroBin));
                double fitMax = h_evtDeltaT->GetXaxis()->GetXmax();
                if (fitMax > fitMin) {
                    TF1 *fexpo = new TF1("f_evtDelta_expo_p7", "expo", fitMin, fitMax);
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

        // Page 8: Baseline and Sigma vs channel (2,1 canvas)
        {
            TCanvas *c_baseline_sigma = new TCanvas(Form("c_baseline_sigma_Run%s", runNumber.c_str()), Form("Baseline and Sigma - Run %s", runNumber.c_str()), 900, 800);
            c_baseline_sigma->Divide(1, 2);
            // Add page number
            c_baseline_sigma->cd();
            TLatex pageNum; pageNum.SetNDC(true); pageNum.SetTextSize(0.025); pageNum.SetTextAlign(31);
            pageNum.DrawLatex(0.95, 0.03, "Page 8");
            
            // cd(1): baseline vs channel for all detectors
            c_baseline_sigma->cd(1);
            if (c_baseline_all) {
                // Recreate the baseline vs channel plot with better legend positioning
                double yMin = 1e300, yMax = -1e300;
                for (int d=0; d<4; ++d) { for (int ch=0; ch<nChannels; ++ch) { double v = baseline.at(d).at(ch); if (v<yMin) yMin=v; if (v>yMax) yMax=v; } }
                if (!(yMax>yMin)) { yMin=0; yMax=1; }
                double pad = 0.05*(yMax-yMin+1e-6);
                TH1F *frame_base = new TH1F("frame_base_p7", Form("Baseline vs channel - Run %s;Channel;Baseline", runNumber.c_str()), nChannels, 0, nChannels);
                frame_base->SetStats(0);
                frame_base->GetYaxis()->SetRangeUser(yMin-pad, yMax+pad);
                frame_base->Draw();
                auto leg_base = new TLegend(0.75, 0.15, 0.95, 0.35); leg_base->SetBorderSize(0); leg_base->SetFillStyle(0); leg_base->SetTextSize(0.03);
                for (int d=0; d<4; ++d) { 
                    auto gr=g_baseline->at(d); 
                    gr->SetMarkerStyle(detMarkers[d]); 
                    gr->SetMarkerSize(0.6); 
                    gr->SetMarkerColor(detColors[d]); 
                    gr->SetLineColor(detColors[d]); 
                    gr->Draw("P SAME"); 
                    leg_base->AddEntry(gr, Form("D%d", d), "lp"); 
                }
                leg_base->Draw();
            }
            
            // cd(2): sigma vs channel
            c_baseline_sigma->cd(2);
            if (c_sigma_all) {
                // Recreate the sigma vs channel plot with better legend positioning
                double yMin_sig = 1e300, yMax_sig = -1e300;
                for (int d=0; d<4; ++d) { for (int ch=0; ch<nChannels; ++ch) { double v = baseline_sigma.at(d).at(ch); if (v<yMin_sig) yMin_sig=v; if (v>yMax_sig) yMax_sig=v; } }
                if (!(yMax_sig>yMin_sig)) { yMin_sig=0; yMax_sig=1; }
                double pad_sig = 0.05*(yMax_sig-yMin_sig+1e-6);
                TH1F *frame_sig = new TH1F("frame_sig_p7", Form("Sigma vs channel - Run %s;Channel;Sigma", runNumber.c_str()), nChannels, 0, nChannels);
                frame_sig->SetStats(0);
                frame_sig->GetYaxis()->SetRangeUser(yMin_sig-pad_sig, yMax_sig+pad_sig);
                frame_sig->Draw();
                auto leg_sig = new TLegend(0.75, 0.15, 0.95, 0.35); leg_sig->SetBorderSize(0); leg_sig->SetFillStyle(0); leg_sig->SetTextSize(0.03);
                for (int d=0; d<4; ++d) { 
                    auto gr=g_sigma->at(d); 
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
            c_baseline_sigma->SaveAs(output_filename_report.c_str());
        }

        // Page 9: Firing channels 
        c_channelsFiring->cd();
        TLatex pageNum2; pageNum2.SetNDC(true); pageNum2.SetTextSize(0.025); pageNum2.SetTextAlign(31);
    pageNum2.DrawLatex(0.95, 0.03, "Page 9");
        c_channelsFiring->Update();
        // Close the PDF on Page 9 now that Page 10 is removed
        c_channelsFiring->SaveAs((output_filename_report + ")").c_str());    // Clean sample scatter pages removed per request
        
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
