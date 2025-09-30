// groupBeam.cpp

#include "cppLibs.h"
#include "rootLibs.h"

#include "CmdLineParser.h"
#include "Logger.h"

LoggerInit([]{
  Logger::getUserHeader() << "[" << FILENAME << "]";
});

// Structure to hold beam configuration
struct BeamConfig {
    std::string date;        // YYYY-MM-DD
    std::string time;        // HH:MM
    double energy;           // GeV (+ or -)
    std::string target;
    std::string details;
    
    // Convert to timestamp (seconds since epoch) + offset within day
    std::pair<time_t, int> getTimestamp() const {
        std::tm tm = {};
        std::istringstream ss(date + " " + time);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M");
        return {std::mktime(&tm), tm.tm_hour * 3600 + tm.tm_min * 60};
    }
    
    // Get energy string for filename
    std::string getEnergyString() const {
        std::ostringstream oss;
        if (energy > 0) oss << "+";
        oss << std::fixed << std::setprecision(1) << energy << "GeV";
        return oss.str();
    }
};

// Function to parse beam_settings.dat
std::vector<BeamConfig> parseBeamSettings(const std::string& filename) {
    std::vector<BeamConfig> configs;
    std::ifstream file(filename);
    std::string line;
    
    if (!file.is_open()) {
        LogError << "Cannot open beam settings file: " << filename << std::endl;
        return configs;
    }
    
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line.find("Date") != std::string::npos || 
            line.find("---") != std::string::npos) continue;
            
        std::istringstream iss(line);
        std::string token;
        std::vector<std::string> tokens;
        
        // Split by | delimiter
        while (std::getline(iss, token, '|')) {
            // Trim whitespace
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);
            tokens.push_back(token);
        }
        
        if (tokens.size() >= 7) {
            BeamConfig config;
            config.date = tokens[0];
            config.time = tokens[1];
            try {
                config.energy = std::stod(tokens[2]);
            } catch (...) {
                continue; // Skip malformed entries
            }
            config.target = tokens[3];
            config.details = tokens[6];
            configs.push_back(config);
        }
    }
    
    LogInfo << "Loaded " << configs.size() << " beam configurations" << std::endl;
    return configs;
}

// Function to parse filename and extract base UTC time
std::tm parseFilenameTime(const std::string& filename) {
    std::tm baseTime = {};
    
    // Extract date and time from filename like SCD_RUN00500_BEAM_20250902_121516
    size_t datePos = filename.find("_BEAM_");
    if (datePos != std::string::npos) {
        datePos += 6; // Skip "_BEAM_"
        if (datePos + 15 <= filename.length()) {
            std::string dateStr = filename.substr(datePos, 8);     // YYYYMMDD
            std::string timeStr = filename.substr(datePos + 9, 6); // HHMMSS
            
            baseTime.tm_year = std::stoi(dateStr.substr(0, 4)) - 1900;
            baseTime.tm_mon = std::stoi(dateStr.substr(4, 2)) - 1;
            baseTime.tm_mday = std::stoi(dateStr.substr(6, 2));
            baseTime.tm_hour = std::stoi(timeStr.substr(0, 2));
            baseTime.tm_min = std::stoi(timeStr.substr(2, 2));
            baseTime.tm_sec = std::stoi(timeStr.substr(4, 2));
            
            LogInfo << "Parsed filename time: " << std::put_time(&baseTime, "%Y-%m-%d %H:%M:%S UTC") << std::endl;
        }
    }
    
    return baseTime;
}

// Function to convert internal timestamp + base time to CEST
std::tm eventToTime(const std::tm& baseTimeUTC, Long64_t internalTimestamp) {
    // Convert base time to seconds since epoch (UTC)
    std::tm baseTimeCopy = baseTimeUTC;
    time_t baseSeconds = timegm(&baseTimeCopy); // timegm for UTC
    
    // Add internal timestamp (20ns ticks to seconds)
    double tickSeconds = internalTimestamp * 20e-9;
    time_t eventSecondsUTC = baseSeconds + (time_t)tickSeconds;
    
    // Convert to CEST (UTC + 2 hours)
    time_t eventSecondsCEST = eventSecondsUTC + 2 * 3600;
    
    std::tm* eventTime = std::gmtime(&eventSecondsCEST);
    return *eventTime;
}

// Get date string in YYYYMMDD format from filename (not timestamp)
std::string getDateFromFilename(const std::string& filename) {
    size_t datePos = filename.find("_BEAM_");
    if (datePos != std::string::npos) {
        datePos += 6; // Skip "_BEAM_"
        if (datePos + 8 <= filename.length()) {
            return filename.substr(datePos, 8); // YYYYMMDD
        }
    }
    return "unknown";
}

// Find beam configuration for given event time
BeamConfig findBeamConfig(const std::tm& eventTime, const std::vector<BeamConfig>& configs) {
    time_t eventTimestamp = timegm(const_cast<std::tm*>(&eventTime));
    
    // Find the most recent configuration before this timestamp
    BeamConfig bestConfig;
    time_t bestTime = 0;
    
    for (const auto& config : configs) {
        auto [configTime, configOffset] = config.getTimestamp();
        if (configTime <= eventTimestamp && configTime > bestTime) {
            bestTime = configTime;
            bestConfig = config;
        }
    }
    
    return bestConfig;
}

// Structure to hold event data for copying
struct EventData {
    Int_t event_index;
    Long64_t evt_size, fw_version, trigger_number, board_id, timestamp, ext_timestamp, trigger_id, file_offset;
    std::vector<std::vector<float>> detectorData;
    std::vector<std::vector<float>> rawDetectorData;
    std::vector<std::vector<float>> pedestalData;
    std::vector<std::vector<float>> sigmaData;
    
    // Clusters data
    std::vector<int> cluster_detector;
    std::vector<int> cluster_start_ch;
    std::vector<int> cluster_end_ch;
    std::vector<int> cluster_size;
    std::vector<float> cluster_amplitude;
    
    EventData() : detectorData(4), rawDetectorData(4), pedestalData(4), sigmaData(4) {}
};

int main(int argc, char* argv[]) {
    CmdLineParser clp;

    clp.getDescription() << "> This program groups _clusters.root files by date and beam energy based on timestamps." << std::endl;

    clp.addDummyOption("Main options");
    clp.addOption("inputDir", {"-i", "--input"}, "Input directory containing _clusters.root files");
    clp.addOption("outputDir", {"-o", "--output"}, "Output directory for grouped files");
    clp.addOption("beamSettings", {"-b", "--beam-settings"}, "Beam settings file (default: parameters/beam_settings.dat)");

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
    
    std::string inputDir = clp.getOptionVal<std::string>("inputDir");
    std::string outputDir = clp.getOptionVal<std::string>("outputDir");
    std::string beamSettingsFile = "parameters/beam_settings.dat";
    if (clp.isOptionTriggered("beamSettings")) {
        beamSettingsFile = clp.getOptionVal<std::string>("beamSettings");
    }

    // Create output directory
    std::filesystem::create_directories(outputDir);

    // Parse beam configurations
    auto beamConfigs = parseBeamSettings(beamSettingsFile);
    if (beamConfigs.empty()) {
        LogError << "No beam configurations loaded!" << std::endl;
        return 1;
    }

    // Find all _clusters.root files
    std::vector<std::string> inputFiles;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(inputDir)) {
        if (entry.is_regular_file() && entry.path().string().find("_clusters.root") != std::string::npos) {
            // Skip CAL files, only process BEAM files
            if (entry.path().string().find("_BEAM_") != std::string::npos) {
                inputFiles.push_back(entry.path().string());
            }
        }
    }

    std::sort(inputFiles.begin(), inputFiles.end());
    LogInfo << "Found " << inputFiles.size() << " formatted ROOT files" << std::endl;

    // Map to store output files: date_energy -> TFile*
    std::map<std::string, TFile*> outputFiles;
    std::map<std::string, TTree*> outputTrees_eventInfo;
    std::map<std::string, TTree*> outputTrees_clusters;
    std::map<std::string, std::vector<TTree*>> outputTrees_detectors;
    std::map<std::string, std::vector<TTree*>> outputTrees_rawDetectors;
    std::map<std::string, std::vector<TTree*>> outputTrees_pedestals;
    std::map<std::string, std::vector<TTree*>> outputTrees_sigmas;

    const int nDetectors = 4;
    const int nChannels = 384;

    // Process each input file
    for (const auto& inputFile : inputFiles) {
        LogInfo << "Processing: " << inputFile << std::endl;
        
        TFile* inFile = new TFile(inputFile.c_str(), "READ");
        if (!inFile->IsOpen()) {
            LogError << "Cannot open file: " << inputFile << std::endl;
            continue;
        }

        // Get trees
        TTree* eventInfoTree = (TTree*)inFile->Get("event_info");
        if (!eventInfoTree) {
            LogError << "No event_info tree in file: " << inputFile << std::endl;
            inFile->Close();
            continue;
        }

        TTree* clustersTree = (TTree*)inFile->Get("clusters");
        if (!clustersTree) {
            LogError << "No clusters tree in file: " << inputFile << std::endl;
            inFile->Close();
            continue;
        }

        std::vector<TTree*> detectorTrees(nDetectors), rawDetectorTrees(nDetectors);
        std::vector<TTree*> pedestalTrees(nDetectors), sigmaTrees(nDetectors);
        
        for (int d = 0; d < nDetectors; ++d) {
            detectorTrees[d] = (TTree*)inFile->Get(Form("detector%d", d));
            rawDetectorTrees[d] = (TTree*)inFile->Get(Form("raw_detector%d", d));
            pedestalTrees[d] = (TTree*)inFile->Get(Form("pedestal%d", d));
            sigmaTrees[d] = (TTree*)inFile->Get(Form("sigma%d", d));
        }

        // Set up branches for reading
        EventData eventData;
        eventInfoTree->SetBranchAddress("event_index", &eventData.event_index);
        eventInfoTree->SetBranchAddress("evt_size", &eventData.evt_size);
        eventInfoTree->SetBranchAddress("fw_version", &eventData.fw_version);
        eventInfoTree->SetBranchAddress("trigger_number", &eventData.trigger_number);
        eventInfoTree->SetBranchAddress("board_id", &eventData.board_id);
        eventInfoTree->SetBranchAddress("timestamp", &eventData.timestamp);
        eventInfoTree->SetBranchAddress("ext_timestamp", &eventData.ext_timestamp);
        eventInfoTree->SetBranchAddress("trigger_id", &eventData.trigger_id);
        eventInfoTree->SetBranchAddress("file_offset", &eventData.file_offset);

        // Set up clusters tree branches with pointers
        std::vector<int>* detectorPtr = &eventData.cluster_detector;
        std::vector<int>* startChPtr = &eventData.cluster_start_ch;
        std::vector<int>* endChPtr = &eventData.cluster_end_ch;
        std::vector<int>* sizePtr = &eventData.cluster_size;
        std::vector<float>* amplitudePtr = &eventData.cluster_amplitude;
        
        clustersTree->SetBranchAddress("detector", &detectorPtr);
        clustersTree->SetBranchAddress("start_ch", &startChPtr);
        clustersTree->SetBranchAddress("end_ch", &endChPtr);
        clustersTree->SetBranchAddress("size", &sizePtr);
        clustersTree->SetBranchAddress("amplitude", &amplitudePtr);

        // Set up detector data branches
        std::vector<std::vector<float>*> detDataPtrs(nDetectors), rawDetDataPtrs(nDetectors);
        std::vector<std::vector<float>*> pedDataPtrs(nDetectors), sigDataPtrs(nDetectors);
        
        for (int d = 0; d < nDetectors; ++d) {
            detDataPtrs[d] = new std::vector<float>();
            rawDetDataPtrs[d] = new std::vector<float>();
            pedDataPtrs[d] = new std::vector<float>();
            sigDataPtrs[d] = new std::vector<float>();
            
            if (detectorTrees[d]) detectorTrees[d]->SetBranchAddress("data", &detDataPtrs[d]);
            if (rawDetectorTrees[d]) rawDetectorTrees[d]->SetBranchAddress("raw_data", &rawDetDataPtrs[d]);
            if (pedestalTrees[d]) pedestalTrees[d]->SetBranchAddress("pedestal", &pedDataPtrs[d]);
            if (sigmaTrees[d]) sigmaTrees[d]->SetBranchAddress("sigma", &sigDataPtrs[d]);
        }

        // Read pedestals and sigmas (static data)
        for (int d = 0; d < nDetectors; ++d) {
            if (pedestalTrees[d] && pedestalTrees[d]->GetEntries() > 0) {
                pedestalTrees[d]->GetEntry(0);
                eventData.pedestalData[d] = *pedDataPtrs[d];
            }
            if (sigmaTrees[d] && sigmaTrees[d]->GetEntries() > 0) {
                sigmaTrees[d]->GetEntry(0);
                eventData.sigmaData[d] = *sigDataPtrs[d];
            }
        }

        // Parse base time from filename
        std::tm baseTime = parseFilenameTime(inputFile);
        
        // Process events
        Long64_t nEntries = eventInfoTree->GetEntries();
        for (Long64_t entry = 0; entry < nEntries; ++entry) {
            eventInfoTree->GetEntry(entry);
            clustersTree->GetEntry(entry);
            
            // Read detector data for this event
            for (int d = 0; d < nDetectors; ++d) {
                if (detectorTrees[d]) {
                    detectorTrees[d]->GetEntry(entry);
                    eventData.detectorData[d] = *detDataPtrs[d];
                }
                if (rawDetectorTrees[d]) {
                    rawDetectorTrees[d]->GetEntry(entry);
                    eventData.rawDetectorData[d] = *rawDetDataPtrs[d];
                }
            }

            // Calculate actual event time (base time + internal timestamp)
            std::tm eventTime = eventToTime(baseTime, eventData.timestamp);
            
            // Determine beam configuration and output file
            BeamConfig config = findBeamConfig(eventTime, beamConfigs);
            std::string dateStr = getDateFromFilename(inputFile);  // Use date from filename
            std::string energyStr = config.getEnergyString();
            std::string outputKey = dateStr + "_" + energyStr;

            if (verbose && entry % 1000 == 0) {
                LogInfo << "Event " << entry << " -> " << outputKey << std::endl;
            }

            // Create output file if needed
            if (outputFiles.find(outputKey) == outputFiles.end()) {
                std::string outputFileName = outputDir + "/" + outputKey + ".root";
                LogInfo << "Creating output file: " << outputFileName << std::endl;
                
                outputFiles[outputKey] = new TFile(outputFileName.c_str(), "RECREATE");
                
                // Create event_info tree
                outputTrees_eventInfo[outputKey] = new TTree("event_info", "event_info");
                TTree* outEventTree = outputTrees_eventInfo[outputKey];
                outEventTree->Branch("event_index", &eventData.event_index, "event_index/I");
                outEventTree->Branch("evt_size", &eventData.evt_size, "evt_size/L");
                outEventTree->Branch("fw_version", &eventData.fw_version, "fw_version/L");
                outEventTree->Branch("trigger_number", &eventData.trigger_number, "trigger_number/L");
                outEventTree->Branch("board_id", &eventData.board_id, "board_id/L");
                outEventTree->Branch("timestamp", &eventData.timestamp, "timestamp/L");
                outEventTree->Branch("ext_timestamp", &eventData.ext_timestamp, "ext_timestamp/L");
                outEventTree->Branch("trigger_id", &eventData.trigger_id, "trigger_id/L");
                outEventTree->Branch("file_offset", &eventData.file_offset, "file_offset/L");

                // Create clusters tree
                outputTrees_clusters[outputKey] = new TTree("clusters", "Clusters per event");
                TTree* outClustersTree = outputTrees_clusters[outputKey];
                outClustersTree->Branch("detector", &eventData.cluster_detector);
                outClustersTree->Branch("start_ch", &eventData.cluster_start_ch);
                outClustersTree->Branch("end_ch", &eventData.cluster_end_ch);
                outClustersTree->Branch("size", &eventData.cluster_size);
                outClustersTree->Branch("amplitude", &eventData.cluster_amplitude);

                // Create detector trees
                outputTrees_detectors[outputKey].resize(nDetectors);
                outputTrees_rawDetectors[outputKey].resize(nDetectors);
                outputTrees_pedestals[outputKey].resize(nDetectors);
                outputTrees_sigmas[outputKey].resize(nDetectors);

                for (int d = 0; d < nDetectors; ++d) {
                    // Detector data trees
                    outputTrees_detectors[outputKey][d] = new TTree(Form("detector%d", d), Form("Baseline-subtracted data detector %d", d));
                    outputTrees_detectors[outputKey][d]->Branch("data", &eventData.detectorData[d]);

                    outputTrees_rawDetectors[outputKey][d] = new TTree(Form("raw_detector%d", d), Form("Raw data detector %d", d));
                    outputTrees_rawDetectors[outputKey][d]->Branch("raw_data", &eventData.rawDetectorData[d]);

                    // Static data trees (write once)
                    outputTrees_pedestals[outputKey][d] = new TTree(Form("pedestal%d", d), Form("Pedestals detector %d", d));
                    outputTrees_pedestals[outputKey][d]->Branch("pedestal", &eventData.pedestalData[d]);
                    outputTrees_pedestals[outputKey][d]->Fill();

                    outputTrees_sigmas[outputKey][d] = new TTree(Form("sigma%d", d), Form("Sigma detector %d", d));
                    outputTrees_sigmas[outputKey][d]->Branch("sigma", &eventData.sigmaData[d]);
                    outputTrees_sigmas[outputKey][d]->Fill();
                }
            }

            // Fill trees
            outputTrees_eventInfo[outputKey]->Fill();
            outputTrees_clusters[outputKey]->Fill();
            for (int d = 0; d < nDetectors; ++d) {
                outputTrees_detectors[outputKey][d]->Fill();
                outputTrees_rawDetectors[outputKey][d]->Fill();
            }
        }

        // Clean up
        for (int d = 0; d < nDetectors; ++d) {
            delete detDataPtrs[d];
            delete rawDetDataPtrs[d];
            delete pedDataPtrs[d];
            delete sigDataPtrs[d];
        }
        
        inFile->Close();
        delete inFile;
    }

    // Close all output files
    for (auto& [key, file] : outputFiles) {
        LogInfo << "Finalizing file: " << key << ".root" << std::endl;
        file->Write();
        file->Close();
        delete file;
    }

    LogInfo << "Grouping complete! Created " << outputFiles.size() << " output files." << std::endl;
    return 0;
}