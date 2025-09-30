// runReport.cpp

#include "cppLibs.h"
#include "rootLibs.h"
#include "utility.h"

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
        // Declare variables at top of main
        std::string runNumber = "unknown";
        std::string runTimeUTC = "";
        std::string runEnergy = "";
        std::string inputFileBase = "";
        CmdLineParser commandLineParser;

        commandLineParser.getDescription() << "> This program reads a clusters ROOT file and generates a PDF report." << std::endl;

        commandLineParser.addDummyOption("Main options");
        commandLineParser.addOption("inputRootFile", {"-i", "--input"}, "Input clusters ROOT file");
        commandLineParser.addOption("outputDir", {"-o", "--output"}, "Output directory for PDF report");
        commandLineParser.addOption("nSigma", {"-s", "--n-sigma"}, "Number of sigmas used for clustering (for labeling only)");

        commandLineParser.addDummyOption("Triggers");
        commandLineParser.addTriggerOption("verboseMode", {"-v"}, "Run in verbose mode");
        commandLineParser.addTriggerOption("spsRun", {"--sps-run"}, "Produce compact SPS-mode report (minimal pages)");

        commandLineParser.addDummyOption();

        LogInfo << commandLineParser.getDescription().str() << std::endl;
        LogInfo << "Usage: " << std::endl;
        LogInfo << commandLineParser.getConfigSummary() << std::endl << std::endl;

        commandLineParser.parseCmdLine(argc, argv);

        LogThrowIf(commandLineParser.isNoOptionTriggered(), "No option was provided.");

        LogInfo << "Provided arguments: " << std::endl;
        LogInfo << commandLineParser.getValueSummary() << std::endl << std::endl;

        bool verboseMode = commandLineParser.isOptionTriggered("verboseMode");
        bool spsRunMode = commandLineParser.isOptionTriggered("spsRun");

        std::string inputFile = commandLineParser.getOptionVal<std::string>("inputRootFile");
        std::string outputDir = commandLineParser.getOptionVal<std::string>("outputDir");
        int nSigma = 0;
        if (commandLineParser.isOptionTriggered("nSigma")) {
                nSigma = commandLineParser.getOptionVal<int>("nSigma");
        }
        // Assign input_file_base after inputFile is known
        inputFileBase = GetBaseName(inputFile);
        // Extract run number, UTC time, and energy from filename, which is  like SCD_RUN00500_BEAM_20250902_121516_clusters.root
        size_t pos = inputFileBase.find("RUN");
        if (pos != std::string::npos) {
            size_t endPos = inputFileBase.find("_", pos + 3);
            if (endPos != std::string::npos) {
                runNumber = inputFileBase.substr(pos + 3, endPos - pos - 3);
            }
            // Try to extract UTC time (adjusted for pattern like RUNxxxx_BEAM_YYYYMMDD_HHMMSS_clusters.root)
            size_t beamPos = inputFileBase.find("_BEAM_");
            if (beamPos != std::string::npos) {
                size_t dateStart = beamPos + 6;
                if (dateStart + 15 <= inputFileBase.size()) {
                    // Expecting pattern: YYYYMMDD_HHMMSS
                    std::string dateTime = inputFileBase.substr(dateStart, 15);
                    if (dateTime.size() == 15 && dateTime[8] == '_') {
                        runTimeUTC = dateTime.substr(9, 6); // HHMMSS
                    }
                }
            }
        }
        std::cout << "UTC TIME IS: " << runTimeUTC << std::endl;

        // Try to extract energy for SPS mode (e.g. _plus1.5GeV or _minus0.5GeV)
        size_t epos = inputFileBase.find("plus");
        if (epos == std::string::npos) epos = inputFileBase.find("minus");
        if (epos != std::string::npos) {
                size_t endE = inputFileBase.find("GeV", epos);
                if (endE != std::string::npos) {
                        std::string sign = (inputFileBase[epos] == 'p') ? "+" : "-";
                        std::string val = inputFileBase.substr(epos+4, endE-epos-4);
                        runEnergy = "Run " + sign + val + "GeV";
                        runNumber = sign + val + "GeV";
                }
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
        const int numberOfDetectors = 4;
        const int numberOfChannels = 384;
        TTree *detectorTrees[numberOfDetectors];
        std::vector<float> *detectorData[numberOfDetectors];
        for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                detectorTrees[detectorIndex] = (TTree*)inputRootFile->Get(Form("detector%d", detectorIndex));
                if (!detectorTrees[detectorIndex]) {
                        LogError << "Error: cannot find detector" << detectorIndex << " tree" << std::endl;
                        return 1;
                }
                detectorData[detectorIndex] = new std::vector<float>();
                detectorTrees[detectorIndex]->SetBranchAddress("data", &detectorData[detectorIndex]);
        }

        // Get raw detector trees
        TTree *rawDetectorTrees[numberOfDetectors];
        std::vector<float> *rawDetectorData[numberOfDetectors];
        for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                rawDetectorTrees[detectorIndex] = (TTree*)inputRootFile->Get(Form("raw_detector%d", detectorIndex));
                if (!rawDetectorTrees[detectorIndex]) {
                        LogError << "Error: cannot find raw_detector" << detectorIndex << " tree" << std::endl;
                        return 1;
                }
                rawDetectorData[detectorIndex] = new std::vector<float>();
                rawDetectorTrees[detectorIndex]->SetBranchAddress("raw_data", &rawDetectorData[detectorIndex]);
        }

        // Get sigma trees
        TTree *sigmaTrees[numberOfDetectors];
        std::vector<float> *sigmaData[numberOfDetectors];
        for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                sigmaTrees[detectorIndex] = (TTree*)inputRootFile->Get(Form("sigma%d", detectorIndex));
                if (!sigmaTrees[detectorIndex]) {
                        LogError << "Error: cannot find sigma" << detectorIndex << " tree" << std::endl;
                        return 1;
                }
                sigmaData[detectorIndex] = new std::vector<float>();
                sigmaTrees[detectorIndex]->SetBranchAddress("sigma", &sigmaData[detectorIndex]);
                sigmaTrees[detectorIndex]->GetEntry(0); // sigmas are static
        }

        // Get pedestal trees
        TTree *pedestalTrees[numberOfDetectors];
        std::vector<float> *pedestalData[numberOfDetectors];
        for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                pedestalTrees[detectorIndex] = (TTree*)inputRootFile->Get(Form("pedestal%d", detectorIndex));
                if (!pedestalTrees[detectorIndex]) {
                        LogError << "Error: cannot find pedestal" << detectorIndex << " tree" << std::endl;
                        return 1;
                }
                pedestalData[detectorIndex] = new std::vector<float>();
                pedestalTrees[detectorIndex]->SetBranchAddress("pedestal", &pedestalData[detectorIndex]);
                pedestalTrees[detectorIndex]->GetEntry(0); // pedestals are static
        }

        // Set up cluster branches
        std::vector<int> *clusterDetectors = nullptr;
        std::vector<int> *clusterStartChannels = nullptr;
        std::vector<int> *clusterEndChannels = nullptr;
        std::vector<int> *clusterSizes = nullptr;
        std::vector<float> *clusterAmplitudes = nullptr;

        clusterTree->SetBranchAddress("detector", &clusterDetectors);
        clusterTree->SetBranchAddress("start_ch", &clusterStartChannels);
        clusterTree->SetBranchAddress("end_ch", &clusterEndChannels);
        clusterTree->SetBranchAddress("size", &clusterSizes);
        clusterTree->SetBranchAddress("amplitude", &clusterAmplitudes);

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

        int numberOfEntries = clusterTree->GetEntries();
        LogInfo << "Number of entries: " << numberOfEntries << std::endl;

        // Create histograms for analysis 
        TH1I *histogramClustersPerEvent[numberOfDetectors];
        for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                histogramClustersPerEvent[detectorIndex] = new TH1I(Form("h_clustersPerEvent_D%d", detectorIndex),
                                                                                 Form("Clusters per event - D%d;Clusters;Events", detectorIndex),
                                                                                 11, -0.5, 10.5);
        }

        TH1I *histogramTotalClustersPerEvent = new TH1I("h_totalClustersPerEvent",
                                                                                         "Total clusters per event;Clusters;Events",
                                                                                         21, -0.5, 20.5);

        TH1I *histogramClusterSizes = new TH1I("h_clusterSizes",
                                                                        "Cluster sizes;Size (channels);Count",
                                                                        20, 0.5, 20.5);

        TH1F *histogramClusterAmplitudes = new TH1F("h_clusterAmplitudes",
                                                                                 "Cluster amplitudes;Amplitude;Count",
                                                                                 100, 0, 100);

        TH2I *histogramClusterSizeVsDetector = new TH2I("h_clusterSizeVsDetector",
                                                                                         "Cluster size vs detector;Detector;Size",
                                                                                         4, -0.5, 3.5, 20, 0.5, 20.5);

        // Histograms for firing channels 
        TH1F *histogramFiringChannels[numberOfDetectors];
        for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                histogramFiringChannels[detectorIndex] = new TH1F(Form("h_firingChannels_D%d", detectorIndex),
                                                                             Form("Firing channels (Detector %d);Channel;Counts", detectorIndex),
                                                                             numberOfChannels, 0, numberOfChannels);
        }

        // Histograms for amplitude
        TH1F *histogramAmplitude[numberOfDetectors];
        for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                histogramAmplitude[detectorIndex] = new TH1F(Form("h_amplitude_D%d", detectorIndex),
                                                                    Form("Amplitude (Detector %d);Amplitude;Counts", detectorIndex),
                                                                    100, -50, 150);
        }

        // 2D histograms for amplitude vs channel
        TH2F *histogramAmplitudeVsChannel[numberOfDetectors];
        for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                histogramAmplitudeVsChannel[detectorIndex] = new TH2F(Form("h_amplitudeVsChannel_D%d", detectorIndex),
                                                                                     Form("Amplitude vs Channel (Detector %d);Channel;Amplitude", detectorIndex),
                                                                                     numberOfChannels, 0, numberOfChannels, 100, -50, 150);
        }

        // Reconstructed centers & graphs (created on first use)
        TH2F *histogramReconstructedCenterExactly3 = nullptr;
        TH2F *histogramReconstructedCenter2Or3 = nullptr;
        TGraph *graphReconstructedCentersExactly3 = nullptr;
        TGraph *graphReconstructedCenters2Or3 = nullptr;
        
        // centers axis ranges and binning (defaults same as dataAnalyzer)
        // Declared early so lazy histogram creation in the event loop can use them
        // For reconstructed centers: X axis [-80,60], double bin count
        double centersXMinimum = -80.0, centersXMaximum = 60.0;
        double centersYMinimum = -60.0, centersYMaximum = 70.0;
        int centersBinsX = 50, centersBinsY = 50;
        // Single constant used for the X shift applied during reconstruction and when drawing the active polygon
        double recoXShift = -25.0;

        // Hits per event (across all detectors) similar to dataAnalyzer
        TH1F *histogramHitsInEvent = new TH1F("h_hitsInEvent", "Hits in event;Hits;Counts", 21, -0.5, 20.5);

        // Raw peak histograms
        TH1F *histogramRawPeak[numberOfDetectors][numberOfChannels];
        for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                for (int channelIndex = 0; channelIndex < numberOfChannels; ++channelIndex) {
                        histogramRawPeak[detectorIndex][channelIndex] = new TH1F(Form("h_rawPeak_D%d_Ch%d", detectorIndex, channelIndex),
                                                                                Form("Raw Peak D%d Ch%d;ADC;Counts", detectorIndex, channelIndex),
                                                                                200, 0, 200);
                }
        }

        // 2D Tracking histograms 
        TH2F *histogramClusterPositions = new TH2F("h_clusterPositions",
                                                                                "Cluster positions;Detector;Channel",
                                                                                4, -0.5, 3.5, numberOfChannels, 0, numberOfChannels);

        TH2F *histogramClusterCenters = new TH2F("h_clusterCenters",
                                                                            "Cluster center positions;Detector;Center Channel",
                                                                            4, -0.5, 3.5, numberOfChannels, 0, numberOfChannels);

        TH2F *histogramClustersPerEvent2D = new TH2F("h_clustersPerEvent2D",
                                                                                    "Clusters per event per detector;Detector;Clusters",
                                                                                    4, -0.5, 3.5, 11, -0.5, 10.5);

        // Timestamp graphs and deltaT histogram (match dataAnalyzer style)
        TGraph *graphEventVsTime = new TGraph(); graphEventVsTime->SetName("g_evtVsTime"); graphEventVsTime->SetTitle("Event index vs internal time;Event index;Time since start (s)");
        TGraph *graphExternalEventVsTime = new TGraph(); graphExternalEventVsTime->SetName("g_extEvtVsTime"); graphExternalEventVsTime->SetTitle("Event index vs external time;Event index;Ext time since start (s)");
        TGraph *graphExternalVsInternalTime = new TGraph(); graphExternalVsInternalTime->SetName("g_extVsIntTime"); graphExternalVsInternalTime->SetTitle("External vs internal time;Internal time since start (s);External time since start (s)");
        TH1F *histogramEventDeltaT = new TH1F("h_evtDeltaT", "#Delta t between consecutive events;#Delta t (s);Counts", 100, 0, 1.0);

        // Process events
        int totalEventsWithClusters = 0;
        int totalClusters = 0;
        int eventsWithHits = 0;

        // For timestamps normalization and delta computation
        bool firstTimeSeen = false;
        double t0_internal = 0.0, t0_external = 0.0, previous_t_internal = 0.0;

        for (int entryIndex = 0; entryIndex < numberOfEntries; ++entryIndex) {
                clusterTree->GetEntry(entryIndex);
                eventInfoTree->GetEntry(entryIndex);

                // Read detector data
                for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                        detectorTrees[detectorIndex]->GetEntry(entryIndex);
                        rawDetectorTrees[detectorIndex]->GetEntry(entryIndex);
                }

                if (verboseMode && entryIndex % 1000 == 0) LogInfo << "Processing event " << entryIndex << std::endl;

                // Count clusters per detector for this event
                int clustersPerDetector[numberOfDetectors] = {0, 0, 0, 0};
                int totalClustersThisEvent = clusterDetectors->size();

                if (totalClustersThisEvent > 0) {
                        totalEventsWithClusters++;
                }

                totalClusters += totalClustersThisEvent;

                // Fill per-detector histograms
                for (size_t clusterIndex = 0; clusterIndex < clusterDetectors->size(); ++clusterIndex) {
                        int detectorIndex = (*clusterDetectors)[clusterIndex];
                        if (detectorIndex >= 0 && detectorIndex < numberOfDetectors) {
                                clustersPerDetector[detectorIndex]++;
                                histogramClusterSizes->Fill((*clusterSizes)[clusterIndex]);
                                histogramClusterAmplitudes->Fill((*clusterAmplitudes)[clusterIndex]);
                                histogramClusterSizeVsDetector->Fill(detectorIndex, (*clusterSizes)[clusterIndex]);

                                // Fill 2D tracking histograms
                                int startChannel = (*clusterStartChannels)[clusterIndex];
                                int endChannel = (*clusterEndChannels)[clusterIndex];
                                int centerChannel = (startChannel + endChannel) / 2;

                                // Fill cluster positions (all channels in cluster)
                                for (int channelIndex = startChannel; channelIndex <= endChannel && channelIndex < numberOfChannels; ++channelIndex) {
                                        histogramClusterPositions->Fill(detectorIndex, channelIndex);
                                }

                                // Fill cluster centers
                                histogramClusterCenters->Fill(detectorIndex, centerChannel);

                                // --- gather quick info for reconstruction ---
                                // We'll use detector indices 0..2 as the three planes
                                // Store the center channel per detector when available
                                // (We'll run the reconstruction below after iterating clusters)
                        }
                }

                // Simple reconstruction of (x,y) from up to 3 strip-plane centers
                {
                        const int geometryDetectorNumber = 3;
                        double stripAnglesDegrees[geometryDetectorNumber] = {-15.0, +15.0, 0.0};
                        double normalAnglesRadians[geometryDetectorNumber];
                        for (int i=0;i<geometryDetectorNumber;i++) normalAnglesRadians[i] = (stripAnglesDegrees[i] + 90.0) * M_PI/180.0;

                        // collect a single center per detector (coarse: take first cluster center encountered)
                        int firstCenterChannel[geometryDetectorNumber]; for (int i=0;i<geometryDetectorNumber;i++) firstCenterChannel[i] = -1;
                        int countsDetector[geometryDetectorNumber]; for (int i=0;i<geometryDetectorNumber;i++) countsDetector[i] = 0;
                        for (size_t clusterIndex = 0; clusterIndex < clusterDetectors->size(); ++clusterIndex) {
                                int detectorIndex = (*clusterDetectors)[clusterIndex];
                                if (detectorIndex < 0 || detectorIndex >= geometryDetectorNumber) continue;
                                int startChannel = (*clusterStartChannels)[clusterIndex];
                                int endChannel = (*clusterEndChannels)[clusterIndex];
                                int centerChannel = (startChannel + endChannel) / 2;
                                if (firstCenterChannel[detectorIndex] < 0) firstCenterChannel[detectorIndex] = centerChannel;
                                countsDetector[detectorIndex]++;
                        }

                        int usedDetectors = 0;
                        double sumCosCos=0, sumSinSin=0, sumCosSin=0, sumUcos=0, sumUsin=0;
                        double cosA=0,sinA=0,uA=0,cosB=0,sinB=0,uB=0; int pairCount=0;
                        double channelPitch = 100.0 / (double)numberOfChannels; // mm per strip (same convention as dataAnalyzer)
                        double uMaximum = 0.5 * numberOfChannels * channelPitch;

                        for (int detectorIndex=0;detectorIndex<geometryDetectorNumber;detectorIndex++) {
                                if (firstCenterChannel[detectorIndex] >= 0) {
                                        double uMeasured = ((numberOfChannels/2.0) - (firstCenterChannel[detectorIndex] + 0.5)) * channelPitch; // mm
                                        double cosValue = cos(normalAnglesRadians[detectorIndex]);
                                        double sinValue = sin(normalAnglesRadians[detectorIndex]);
                                        double uValue = uMeasured; // no plane offset by default
                                        sumCosCos += cosValue*cosValue; sumSinSin += sinValue*sinValue; sumCosSin += cosValue*sinValue; sumUcos += uValue*cosValue; sumUsin += uValue*sinValue; usedDetectors++;
                                        if (pairCount==0) { cosA=cosValue; sinA=sinValue; uA=uValue; pairCount=1; } else if (pairCount==1) { cosB=cosValue; sinB=sinValue; uB=uValue; pairCount=2; }
                                }
                        }

                        double reconstructedX=0, reconstructedY=0; bool hasXY=false;
                        if (usedDetectors >= 2) {
                                if (usedDetectors == 2) {
                                        double determinant = cosA*sinB - sinA*cosB;
                                        if (fabs(determinant) > 1e-9) {
                                                reconstructedX = ( uA*sinB - sinA*uB)/determinant + recoXShift;
                                                reconstructedY = ( cosA*uB - uA*cosB)/determinant;
                                                hasXY = true;
                                        }
                                } else {
                                        double determinant = sumCosCos*sumSinSin - sumCosSin*sumCosSin;
                                        if (fabs(determinant) > 1e-12) {
                                                reconstructedX = ( sumSinSin*sumUcos - sumCosSin*sumUsin)/determinant + recoXShift;
                                                reconstructedY = ( sumCosCos*sumUsin - sumCosSin*sumUcos)/determinant;
                                                hasXY = true;
                                        }
                                }
                        }

                        if (hasXY) {
                                // lazy create histograms/graphs
                                if (!histogramReconstructedCenter2Or3) {
                                        histogramReconstructedCenter2Or3 = new TH2F("h_recoCenter_2to3", "Reconstructed centers (2 or 3 clusters);X [mm];Y [mm]", centersBinsX, centersXMinimum, centersXMaximum, centersBinsY, centersYMinimum, centersYMaximum);
                                        graphReconstructedCenters2Or3 = new TGraph(); graphReconstructedCenters2Or3->SetName("g_recoCenters_2to3");
                                }
                                histogramReconstructedCenter2Or3->Fill(reconstructedX, reconstructedY);
                                graphReconstructedCenters2Or3->SetPoint(graphReconstructedCenters2Or3->GetN(), reconstructedX, reconstructedY);
                                bool exactlyThreeClusters = (countsDetector[0] == 1 && countsDetector[1] == 1 && countsDetector[2] == 1);
                                if (exactlyThreeClusters) {
                                        if (!histogramReconstructedCenterExactly3) {
                                                histogramReconstructedCenterExactly3 = new TH2F("h_recoCenter_3", "Reconstructed centers (exactly 3 clusters);X [mm];Y [mm]", centersBinsX, centersXMinimum, centersXMaximum, centersBinsY, centersYMinimum, centersYMaximum);
                                                graphReconstructedCentersExactly3 = new TGraph(); graphReconstructedCentersExactly3->SetName("g_recoCenters_3");
                                        }
                                        histogramReconstructedCenterExactly3->Fill(reconstructedX, reconstructedY);
                                        graphReconstructedCentersExactly3->SetPoint(graphReconstructedCentersExactly3->GetN(), reconstructedX, reconstructedY);
                                }
                        }
                }

                // Fill histograms
                for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                        // Fill per-detector clusters histogram only when non-zero
                        if (clustersPerDetector[detectorIndex] != 0) histogramClustersPerEvent[detectorIndex]->Fill(clustersPerDetector[detectorIndex]);
                        histogramClustersPerEvent2D->Fill(detectorIndex, clustersPerDetector[detectorIndex]);
                }
                if (totalClustersThisEvent > 0) histogramTotalClustersPerEvent->Fill(totalClustersThisEvent);

        // Fill firing channels and amplitude histograms 
        bool hasHits = false;
        int hitsThisEvent = 0;
                for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                        for (int channelIndex = 0; channelIndex < numberOfChannels && channelIndex < (int)detectorData[detectorIndex]->size(); ++channelIndex) {
                                float value = (*detectorData[detectorIndex])[channelIndex];
                                float sigma = (*sigmaData[detectorIndex])[channelIndex];
                                float rawValue = (*rawDetectorData[detectorIndex])[channelIndex];

                                // Fill raw peak histograms
                                if (channelIndex < numberOfChannels) {
                                        histogramRawPeak[detectorIndex][channelIndex]->Fill(rawValue);
                                }

                                // Check if this is a hit (above threshold)
                                double thresholdNSigma = (nSigma > 0 ? (double)nSigma : 5.0);
                                if (!ChannelMask(channelIndex) && value > thresholdNSigma * sigma) {  // Using configurable sigma threshold
                                        hasHits = true;
                                        hitsThisEvent++;
                                        histogramFiringChannels[detectorIndex]->Fill(channelIndex);
                                        histogramAmplitude[detectorIndex]->Fill(value);
                                        histogramAmplitudeVsChannel[detectorIndex]->Fill(channelIndex, value);
                                }
                        }
                }
                if (hasHits) eventsWithHits++;
                histogramHitsInEvent->Fill(hitsThisEvent);

                // Timestamps handling
                // Convert timestamps to seconds assuming 1 tick = 1 microsecond if units are unknown
                // If units are already seconds, scaling factors of 1 won't hurt visually
                double timeInternal = (double)timestamp * 1e-6;
                double timeExternal = (double)ext_timestamp * 1e-6;
                if (!firstTimeSeen) {
                        firstTimeSeen = true; t0_internal = timeInternal; t0_external = timeExternal; previous_t_internal = timeInternal;
                }
                double relativeTimeInternal = timeInternal - t0_internal;
                double relativeTimeExternal = timeExternal - t0_external;
                graphEventVsTime->SetPoint(graphEventVsTime->GetN(), entryIndex, relativeTimeInternal);
                graphExternalEventVsTime->SetPoint(graphExternalEventVsTime->GetN(), entryIndex, relativeTimeExternal);
                graphExternalVsInternalTime->SetPoint(graphExternalVsInternalTime->GetN(), relativeTimeInternal, relativeTimeExternal);
                double deltaTime = timeInternal - previous_t_internal; previous_t_internal = timeInternal;
                if (deltaTime >= 0) histogramEventDeltaT->Fill(deltaTime);
        }

        LogInfo << "Analysis complete:" << std::endl;
        LogInfo << "Total events: " << numberOfEntries << std::endl;
        LogInfo << "Events with clusters: " << totalEventsWithClusters << std::endl;
        LogInfo << "Events with hits: " << eventsWithHits << std::endl;
        LogInfo << "Total clusters: " << totalClusters << std::endl;

        // Create PDF report 
        std::string outputFilenameReport;
        if (nSigma > 0) {
                outputFilenameReport = outputDir + "/" + inputFileBase + "_" + std::to_string(nSigma) + "sigma_report.pdf";
        } else {
                outputFilenameReport = outputDir + "/" + inputFileBase + "_report.pdf";
        }

        LogInfo << "Output PDF report: " << outputFilenameReport << std::endl;

        // Ensure output directory exists and is writable
        {
                std::error_code ec;
                std::filesystem::create_directories(outputDir, ec);
                if (ec) {
                        LogError << "Failed to create output directory " << outputDir << ": " << ec.message() << std::endl;
                        return 1;
                }
                const std::string tmpProbe = outputDir + "/.__write_probe";
                {
                        std::ofstream probe(tmpProbe, std::ios::out | std::ios::trunc);
                        if (!probe.good()) {
                                LogError << "Output directory " << outputDir << " is not writable!" << std::endl;
                                return 1;
                        }
                }
                std::remove(tmpProbe.c_str());
        }

        // Root app
        TApplication *app = new TApplication("app", &argc, argv);

        // Set root style
        gStyle->SetOptStat("emruo");
        gStyle->SetTitleFontSize(0.045);

        // Extract run number from filename
        // runNumber and pos already declared at top of main

        // Define detector colors and markers 
        Color_t detectorColors[numberOfDetectors] = {kRed, kBlue, kGreen+2, kMagenta};
        int detectorMarkers[numberOfDetectors] = {20, 21, 22, 23};

        // Geometry and active-area precomputation (used for plotting overlays)
        const int geometryDetectorNumber = 3;
        const double halfX = 50.0; // mm (physical X half-extent per plane)
        double stripAnglesDegrees[geometryDetectorNumber] = {-15.0, +15.0, 0.0};
        std::vector<double> normalAnglesRadians(geometryDetectorNumber);
        for (int i=0;i<geometryDetectorNumber;i++) normalAnglesRadians[i] = (stripAnglesDegrees[i] + 90.0) * M_PI/180.0;

        // default plane offsets
        std::vector<std::pair<double,double>> planeOffsets(geometryDetectorNumber, {0.0, 0.0});
        // try to read parameters/geometry.json if present (support running from project root or build/ directory)
        try {
                const std::vector<std::string> geomCandidates = {"parameters/geometry.json", "../parameters/geometry.json"};
                std::string geomPath;
                for (const auto &gp : geomCandidates) { if (std::filesystem::exists(gp)) { geomPath = gp; break; } }
                if (!geomPath.empty()) {
                        std::ifstream fgeom(geomPath);
                        if (fgeom) {
                                nlohmann::json gjs; fgeom >> gjs;
                                if (gjs.contains("planeOffsetsMm") && gjs["planeOffsetsMm"].is_array()) {
                                        for (int i = 0; i < geometryDetectorNumber && i < (int)gjs["planeOffsetsMm"].size(); ++i) {
                                                const auto &elt = gjs["planeOffsetsMm"][i];
                                                if (elt.is_array() && elt.size() >= 2 && elt[0].is_number() && elt[1].is_number()) {
                                                        planeOffsets[i].first  = elt[0].get<double>();
                                                        planeOffsets[i].second = elt[1].get<double>();
                                                }
                                        }
                                }
                        }
                }
        } catch (...) { /* ignore */ }

        // compute Umax from channel geometry
        double channelPitch = 100.0 / (double)numberOfChannels; // mm per strip
        double uMaximum = 0.5 * numberOfChannels * channelPitch;

        // Try to override centers axis ranges from parameters/geometry.json if present
        try {
                const std::vector<std::string> geomCandidates = {"parameters/geometry.json", "../parameters/geometry.json"};
                std::string geomPath;
                for (const auto &gp : geomCandidates) { if (std::filesystem::exists(gp)) { geomPath = gp; break; } }
                if (!geomPath.empty()) {
                        std::ifstream fgeom(geomPath);
                        if (fgeom) {
                                nlohmann::json gjs; fgeom >> gjs;
                                if (gjs.contains("centersAxisRanges") && gjs["centersAxisRanges"].is_object()) {
                                        const auto &ax = gjs["centersAxisRanges"];
                                        if (ax.contains("xmin") && ax["xmin"].is_number()) centersXMinimum = ax["xmin"].get<double>();
                                        if (ax.contains("xmax") && ax["xmax"].is_number()) centersXMaximum = ax["xmax"].get<double>();
                                        if (ax.contains("ymin") && ax["ymin"].is_number()) centersYMinimum = ax["ymin"].get<double>();
                                        if (ax.contains("ymax") && ax["ymax"].is_number()) centersYMaximum = ax["ymax"].get<double>();
                                }
                        }
                }
        } catch (...) { /* ignore */ }

        // Log which geometry file is actually present (project root or build/) and current axis/shift values
        std::string geomUsed;
        if (std::filesystem::exists("parameters/geometry.json")) geomUsed = "parameters/geometry.json";
        else if (std::filesystem::exists("../parameters/geometry.json")) geomUsed = "../parameters/geometry.json";
        else geomUsed = "(none)";
        LogInfo << "Geometry file used: " << geomUsed << std::endl;
        LogInfo << "centersXMinimum=" << centersXMinimum << " centersXMaximum=" << centersXMaximum << " (effective upper forced to 60.0 in plotting)" << std::endl;
        LogInfo << "centersYMinimum=" << centersYMinimum << " centersYMaximum=" << centersYMaximum << std::endl;
        LogInfo << "recoXShift=" << recoXShift << std::endl;

        // compute active polygon (intersection of plane acceptance regions)
        auto computeActiveAreaPolygon = [&](bool flipYCoords){
                struct Line { double A,B,C; };
                std::vector<Line> lines; lines.reserve(12);
                for (int i=0;i<geometryDetectorNumber;i++) {
                        double cosValue = std::cos(normalAnglesRadians[i]);
                        double sinValue = std::sin(normalAnglesRadians[i]);
                        double offset = planeOffsets[i].first * cosValue + planeOffsets[i].second * sinValue;
                        lines.push_back({ cosValue,  sinValue,  uMaximum + offset});
                        lines.push_back({-cosValue, -sinValue,  uMaximum - offset});
                        double x0 = planeOffsets[i].first;
                        lines.push_back({ 1.0, 0.0, x0 + halfX});
                        lines.push_back({-1.0, 0.0, -(x0 - halfX)});
                }
                auto satisfiesAll = [&](double x, double y){
                        for (int i=0;i<geometryDetectorNumber;i++) {
                                double cosValue = std::cos(normalAnglesRadians[i]);
                                double sinValue = std::sin(normalAnglesRadians[i]);
                                double u = (x - planeOffsets[i].first) * cosValue + (y - planeOffsets[i].second) * sinValue;
                                if (std::fabs(u) > uMaximum + 1e-6) return false;
                                if (std::fabs(x - planeOffsets[i].first) > (halfX + 1e-6)) return false;
                        }
                        return true;
                };
                std::vector<std::pair<double,double>> pts;
                for (size_t i=0;i<lines.size();++i) {
                        for (size_t j=i+1;j<lines.size();++j) {
                                double A1=lines[i].A, B1=lines[i].B, C1=lines[i].C;
                                double A2=lines[j].A, B2=lines[j].B, C2=lines[j].C;
                                double det = A1*B2 - A2*B1;
                                if (std::fabs(det) < 1e-10) continue;
                                double x = (C1*B2 - C2*B1)/det;
                                double y = (A1*C2 - A2*C1)/det;
                                if (satisfiesAll(x,y)) pts.emplace_back(x, flipYCoords ? -y : y);
                        }
                }
                auto eqPt = [](const std::pair<double,double>& a, const std::pair<double,double>& b){ return (std::fabs(a.first-b.first) < 1e-4) && (std::fabs(a.second-b.second) < 1e-4); };
                std::vector<std::pair<double,double>> uniq;
                for (auto &p: pts){ bool found=false; for (auto &q: uniq){ if (eqPt(p,q)){found=true;break;} } if (!found) uniq.push_back(p); }
                if (uniq.size() < 3) return uniq;
                double cx=0, cy=0; for (auto &p: uniq){ cx+=p.first; cy+=p.second; } cx/=uniq.size(); cy/=uniq.size();
                std::sort(uniq.begin(), uniq.end(), [&](const auto& a, const auto& b){ double aa = std::atan2(a.second - cy, a.first - cx); double bb = std::atan2(b.second - cy, b.first - cx); return aa < bb; });
                return uniq;
        };
        const auto activePolygonPoints = computeActiveAreaPolygon(false);

        try {
                LogInfo << "Creating multi-page PDF report..." << std::endl;
                auto drawPageTag = [](int page){ TLatex pageNum; pageNum.SetNDC(true); pageNum.SetTextSize(0.025); pageNum.SetTextAlign(31); pageNum.DrawLatex(0.95, 0.03, Form("Page %d", page)); };
                int page = 1;
                // Page 1: Summary page
                TCanvas *canvasSummary = new TCanvas(Form("c_summary_Run%s", runNumber.c_str()),
                                                                                 Form("Run %s Summary", runNumber.c_str()), 800, 600);
                canvasSummary->cd();
                TLatex latex;
                latex.SetNDC(true);
                latex.SetTextSize(0.030);
                double yTop = 0.93;
                const double dyHead = 0.05;

                // Add page number
                latex.SetTextSize(0.025);
                latex.SetTextAlign(31);
                latex.DrawLatex(0.95, 0.03, "Page 1");
                latex.SetTextAlign(11);
                latex.SetTextSize(0.030);

                latex.DrawLatex(0.10, yTop, Form("Run %s cluster summary", runNumber.c_str()));
                yTop -= dyHead;

                // Statistics table
                const double rowH = 0.035;
                double xL = 0.04, xL_num = xL + 0.35, xL_pct = xL + 0.50;
                double yL = yTop - 0.03;

                latex.DrawLatex(xL_num, yL, "number");
                latex.DrawLatex(xL_pct, yL, "percentage");
                yL -= rowH;

                latex.DrawLatex(xL, yL, "Total events");
                latex.DrawLatex(xL_num, yL, Form("%d", numberOfEntries));
                latex.DrawLatex(xL_pct, yL, "100.00%");
                yL -= rowH;

                latex.DrawLatex(xL, yL, "Events with clusters");
                latex.DrawLatex(xL_num, yL, Form("%d", totalEventsWithClusters));
                latex.DrawLatex(xL_pct, yL, Form("%.2f%%", (numberOfEntries > 0) ? (100.0 * totalEventsWithClusters / numberOfEntries) : 0.0));
                yL -= rowH;

                latex.DrawLatex(xL, yL, "Events with hits");
                latex.DrawLatex(xL_num, yL, Form("%d", eventsWithHits));
                latex.DrawLatex(xL_pct, yL, Form("%.2f%%", (numberOfEntries > 0) ? (100.0 * eventsWithHits / numberOfEntries) : 0.0));
                yL -= rowH;

                latex.DrawLatex(xL, yL, "Total clusters");
                latex.DrawLatex(xL_num, yL, Form("%d", totalClusters));
                latex.DrawLatex(xL_pct, yL, Form("%.2f%%", (numberOfEntries > 0) ? (100.0 * totalClusters / numberOfEntries) : 0.0));
                yL -= rowH;

                // Average clusters per event
                double avgClustersPerEvent = (numberOfEntries > 0) ? (double)totalClusters / numberOfEntries : 0.0;
                latex.DrawLatex(xL, yL, "Avg clusters/event");
                latex.DrawLatex(xL_num, yL, Form("%.2f", avgClustersPerEvent));
                yL -= rowH;

                // Add average clusters per detector per event (with clusters)
                yL -= 0.02;
                latex.DrawLatex(xL, yL, "Avg clusters per detector per event (with clusters):");
                yL -= rowH;
                for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                        double totalClustersInDetector = 0.0;
                        for (int b = 1; b <= histogramClustersPerEvent[detectorIndex]->GetNbinsX(); ++b) {
                                totalClustersInDetector += histogramClustersPerEvent[detectorIndex]->GetBinContent(b) * (b - 1); // since bin 1 is 0 clusters, bin 2 is 1, etc.
                        }
                        double avg = (totalEventsWithClusters > 0) ? (totalClustersInDetector / totalEventsWithClusters) : 0.0;
                        latex.DrawLatex(xL, yL, Form("D%d: %.2f", detectorIndex, avg));
                        yL -= rowH;
                }

                // Add CEST time if not SPS mode and runTimeUTC is available
                if (!spsRunMode && !runTimeUTC.empty()) {
                        int hour = 0, minute = 0, second = 0;
                        if (sscanf(runTimeUTC.c_str(), "%2d%2d%2d", &hour, &minute, &second) == 3) {
                                hour = (hour + 2) % 24;
                                char cest[16];
                                // Extract date from inputFileBase (expecting pattern: ..._YYYYMMDD_HHMMSS...)
                                char date[9] = "";
                                size_t datePos = inputFileBase.find("_BEAM_");
                                if (datePos != std::string::npos && datePos + 6 + 8 <= inputFileBase.size()) {
                                    std::string dateStr = inputFileBase.substr(datePos + 6, 8); // YYYYMMDD
                                    strncpy(date, dateStr.c_str(), sizeof(date) - 1);
                                    date[8] = '\0';
                                }
                                snprintf(cest, sizeof(cest), "%s %02d:%02d CEST", date, hour, minute);
                                latex.SetTextSize(0.030);
                                latex.DrawLatex(0.50, 0.93, cest);
                        }
                }

                // If SPS mode, try to read beam configuration from parameters/beam_settings.dat
                if (spsRunMode) {
                        auto trim = [](std::string s){
                                // trim both ends
                                const char *ws = " \t\r\n";
                                size_t start = s.find_first_not_of(ws);
                                size_t end = s.find_last_not_of(ws);
                                if (start == std::string::npos) return std::string("");
                                return s.substr(start, end - start + 1);
                        };
                        struct BeamEntry { std::string date; std::string time; std::string energy; std::string target; std::string cherenkov; std::string collimator; std::string details; };
                        std::vector<BeamEntry> entries;
                        const std::vector<std::string> beamCandidates = {"parameters/beam_settings.dat", "../parameters/beam_settings.dat"};
                        std::string beamPath;
                        for (const auto &bp: beamCandidates) if (std::filesystem::exists(bp)) { beamPath = bp; break; }
                        if (!beamPath.empty()) {
                                std::ifstream fbeam(beamPath);
                                std::string line;
                                bool headerPassed = false;
                                while (std::getline(fbeam, line)) {
                                        std::string ls = trim(line);
                                        if (ls.empty()) continue;
                                        if (ls.size() > 0 && ls[0] == '#') continue;
                                        // header separator line (---) skip
                                        bool allDashes = true; for (char c: ls) if (c != '-' && c != '|' && c != ' ' && c != '\t') { allDashes = false; break; }
                                        if (allDashes) { headerPassed = true; continue; }
                                        if (!headerPassed) continue; // skip until after header separator
                                        // split by '|'
                                        std::vector<std::string> cols;
                                        size_t p = 0;
                                        while (p < line.size()) {
                                                size_t q = line.find('|', p);
                                                if (q == std::string::npos) q = line.size();
                                                cols.push_back(trim(line.substr(p, q - p)));
                                                p = q + 1;
                                        }
                                        if (cols.size() < 7) continue;
                                        BeamEntry be;
                                        be.date = cols[0]; be.time = cols[1]; be.energy = cols[2]; be.target = cols[3]; be.cherenkov = cols[4]; be.collimator = cols[5]; be.details = cols[6];
                                        entries.push_back(be);
                                }
                        }

                        // Try to extract date and energy from inputFileBase
                        std::string queryDate = "", queryEnergy = ""; // date in YYYY-MM-DD, energy like +1.5 or -3.0
                        if (inputFileBase.size() >= 8) {
                                std::string d = inputFileBase.substr(0,8);
                                if (d.find_first_not_of("0123456789") == std::string::npos) {
                                        // format YYYYMMDD to YYYY-MM-DD
                                        queryDate = d.substr(0,4) + "-" + d.substr(4,2) + "-" + d.substr(6,2);
                                }
                        }
                        // energy: look for + or - followed by digits and optional decimal, possibly followed by "GeV"
                        size_t epos = inputFileBase.find_first_of("+-", 8);
                        if (epos != std::string::npos) {
                                size_t gpos = inputFileBase.find("GeV", epos);
                                if (gpos != std::string::npos && gpos > epos) {
                                        queryEnergy = inputFileBase.substr(epos, gpos - epos);
                                } else {
                                        // take until next underscore or end
                                        size_t endp = inputFileBase.find_first_of("_", epos+1);
                                        if (endp == std::string::npos) endp = inputFileBase.size();
                                        queryEnergy = inputFileBase.substr(epos, endp - epos);
                                }
                                // trim
                                queryEnergy = trim(queryEnergy);
                        }

                        // Find best matching entry: prefer date+energy, then date, then energy
                        int foundIndex = -1;
                        for (size_t i=0;i<entries.size();++i) {
                                if (!queryDate.empty() && !queryEnergy.empty()) {
                                        if (entries[i].date == queryDate && entries[i].energy.find(queryEnergy) != std::string::npos) { foundIndex = (int)i; break; }
                                }
                        }
                        if (foundIndex < 0 && !queryDate.empty()) {
                                for (size_t i=0;i<entries.size();++i) if (entries[i].date == queryDate) { foundIndex = (int)i; break; }
                        }
                        if (foundIndex < 0 && !queryEnergy.empty()) {
                                for (size_t i=0;i<entries.size();++i) if (entries[i].energy.find(queryEnergy) != std::string::npos) { foundIndex = (int)i; break; }
                        }

                        // Print result on summary page
                        latex.SetTextSize(0.026);
                        latex.SetTextAlign(11);
                        double yBeam = yTop - 0.5; // reuse yTop area
                        if (foundIndex >= 0) {
                                const auto &be = entries[foundIndex];
                                latex.DrawLatex(xL, yBeam, "Beam settings:"); yBeam -= rowH;
                                latex.DrawLatex(xL, yBeam, Form("Time: %s", be.time.c_str())); yBeam -= rowH;
                                latex.DrawLatex(xL, yBeam, Form("Target: %s", be.target.c_str())); yBeam -= rowH;
                                latex.DrawLatex(xL, yBeam, Form("Cherenkov: %s", be.cherenkov.c_str())); yBeam -= rowH;
                                latex.DrawLatex(xL, yBeam, Form("Collimator: %s", be.collimator.c_str())); yBeam -= rowH;
                                latex.DrawLatex(xL, yBeam, Form("Notes: %s", be.details.c_str())); yBeam -= rowH;
                        } else if (!entries.empty()) {
                                latex.DrawLatex(xL, yBeam, "Beam settings: (no exact match)"); yBeam -= rowH;
                                // print first few entries as reference
                                for (size_t i=0;i<entries.size() && i<3; ++i) {
                                        const auto &be = entries[i];
                                        latex.DrawLatex(xL, yBeam, Form("%s %s: T=%s, C=%s, Col=%s", be.date.c_str(), be.energy.c_str(), be.target.c_str(), be.cherenkov.c_str(), be.collimator.c_str())); yBeam -= rowH;
                                }
                        } else {
                                latex.DrawLatex(xL, yBeam, "Beam settings: (no beam_settings.dat found)"); yBeam -= rowH;
                        }
                        // restore yTop downward for further summary rows if needed
                        yTop = yBeam - 0.01;
                }



                canvasSummary->Update();
                canvasSummary->SaveAs((outputFilenameReport + "(").c_str());

        if (histogramReconstructedCenterExactly3) {
                // Calculate percent for title
                double percent = (graphReconstructedCentersExactly3 && graphReconstructedCentersExactly3->GetN() > 0 && numberOfEntries > 0) ? (100.0 * graphReconstructedCentersExactly3->GetN() / numberOfEntries) : 0.0;
                std::string title = !runEnergy.empty() ? runEnergy : ("Run " + runNumber);
                std::string title3 = Form("Tracking (1 cluster/Det) [%.1f%% of events] - %s;X [mm];Y [mm]", percent, title.c_str());
                histogramReconstructedCenterExactly3->SetTitle(title3.c_str());
                TCanvas *c = new TCanvas("c_recoCenter_3", Form("Exactly 3 (Run %s)", runNumber.c_str()), 980, 820);
                TPad *padLeft3   = new TPad("padLeft3",   "padLeft3",   0.0, 0.2, 0.2, 1.0);
                TPad *padBottom3 = new TPad("padBottom3", "padBottom3", 0.2, 0.0, 1.0, 0.2);
                TPad *padMain3   = new TPad("padMain3",   "padMain3",   0.2, 0.2, 1.0, 1.0);
                padMain3->SetRightMargin(0.15); padMain3->SetTopMargin(0.16); padMain3->SetBottomMargin(0.12); padMain3->SetLeftMargin(0.12);
                padBottom3->SetTopMargin(0.18); padBottom3->SetBottomMargin(0.35); padBottom3->SetLeftMargin(0.12); padBottom3->SetRightMargin(0.15);
                padLeft3->SetRightMargin(0.05); padLeft3->SetTopMargin(0.05); padLeft3->SetBottomMargin(0.12); padLeft3->SetLeftMargin(0.28);
                c->cd(); padMain3->Draw(); padBottom3->Draw(); padLeft3->Draw();
                // Main 2D plot
                padMain3->cd();
                histogramReconstructedCenterExactly3->GetXaxis()->SetTitle("X [mm]");
                histogramReconstructedCenterExactly3->GetYaxis()->SetTitle("Y [mm]");
                                        // enforce identical axis ranges used for reconstruction so both canvases look identical
                                        // Force the X-axis maximum to 60.0 (user-requested fixed upper limit)
                                        histogramReconstructedCenterExactly3->GetXaxis()->SetRangeUser(centersXMinimum, 60.0);
                                        histogramReconstructedCenterExactly3->GetYaxis()->SetRangeUser(centersYMinimum, centersYMaximum);
                histogramReconstructedCenterExactly3->Draw("COLZ");
                // Mark global origin
                TMarker *centerMarker3 = new TMarker(0.0, 0.0, kFullCircle);
                centerMarker3->SetMarkerColor(kRed); centerMarker3->SetMarkerSize(1.2); centerMarker3->Draw("SAME");
                // Overlay active area polygon
                // Note: do NOT change recoXShift based on spsRunMode here — keep polygon placement identical across modes
                if (activePolygonPoints.size() >= 3) {
                        auto gpoly = new TGraph((int)activePolygonPoints.size()+1);
                        for (int ip=0; ip<(int)activePolygonPoints.size(); ++ip) gpoly->SetPoint(ip, activePolygonPoints[ip].first , activePolygonPoints[ip].second);
                        gpoly->SetPoint((int)activePolygonPoints.size(), activePolygonPoints[0].first, activePolygonPoints[0].second);
                        gpoly->SetLineColor(kBlack); gpoly->SetLineWidth(2); gpoly->SetFillStyle(0); gpoly->Draw("L SAME");
                        auto leg = new TLegend(0.20, 0.68, 0.78, 0.9);
                        leg->SetBorderSize(0); leg->SetFillStyle(0); leg->SetTextSize(0.028);
                        leg->AddEntry(gpoly, "Active area with all 3 detectors", "l"); leg->Draw();
                }
                // Place stats box
                padMain3->Modified(); padMain3->Update();
                if (auto st = dynamic_cast<TPaveStats*>(histogramReconstructedCenterExactly3->GetListOfFunctions()->FindObject("stats"))) {
                        double x1u = 30.0, x2u = 60.0;
                        double y2u = 70.0 - 5.0;
                        double y1u = y2u - 15.0;
                        auto userToNDC = [&](TPad* pad, double ux, double uy){
                                double uxmin = pad->GetUxmin(); double uxmax = pad->GetUxmax();
                                double uymin = pad->GetUymin(); double uymax = pad->GetUymax();
                                double xndc = pad->GetLeftMargin() + (ux-uxmin)/(uxmax-uxmin) * (1.0 - pad->GetLeftMargin() - pad->GetRightMargin());
                                double yndc = pad->GetBottomMargin() + (uy-uymin)/(uymax-uymin) * (1.0 - pad->GetTopMargin() - pad->GetBottomMargin());
                                return std::pair<double,double>(xndc, yndc);
                        };
                        auto p1 = userToNDC(padMain3, x1u, y1u);
                        auto p2 = userToNDC(padMain3, x2u, y2u);
                        double eps = 0.01;
                        double rightLimit = 1.0 - padMain3->GetRightMargin() - eps;
                        if (p2.first > rightLimit) { double shift = p2.first - rightLimit; p1.first -= shift; p2.first -= shift; }
                        double topLimit = 1.0 - padMain3->GetTopMargin() - eps;
                        double bottomPad = padMain3->GetBottomMargin();
                        double availHeight = topLimit - bottomPad;
                        double baseH = p2.second - p1.second;
                        double desiredH = 3.0 * baseH;
                        if (desiredH > 0.95 * availHeight) desiredH = 0.95 * availHeight;
                        if (p2.second > topLimit) p2.second = topLimit;
                        double newY1 = p2.second - desiredH;
                        double minY = padMain3->GetBottomMargin() + 0.70*(1.0 - padMain3->GetTopMargin() - padMain3->GetBottomMargin());
                        if (newY1 < minY) newY1 = minY;
                        double newY2 = newY1 + desiredH;
                        if (newY2 > topLimit) { double dy = newY2 - topLimit; newY1 -= dy; newY2 -= dy; }
                        st->SetX1NDC(p1.first); st->SetX2NDC(p2.first);
                        st->SetY1NDC(newY1);    st->SetY2NDC(newY2);
                }
                // Bottom projection X
                padBottom3->cd();
                TH1D *projX3 = histogramReconstructedCenterExactly3->ProjectionX("h_projX_3");
                projX3->SetTitle("");
                projX3->GetXaxis()->SetLabelSize(0.08);
                projX3->GetYaxis()->SetTitle("");
                projX3->GetYaxis()->SetTitleSize(0.08);
                projX3->GetYaxis()->SetLabelSize(0.0);
                projX3->SetStats(0);
                projX3->SetFillStyle(0);
                projX3->Draw("HIST");
                // Left projection Y
                padLeft3->cd();
                TH1D *projY3 = histogramReconstructedCenterExactly3->ProjectionY("h_projY_3");
                projY3->SetTitle("");
                projY3->GetXaxis()->SetTitle("");
                projY3->GetXaxis()->SetTitleSize(0.08);
                projY3->GetXaxis()->SetLabelSize(0.08);
                projY3->GetYaxis()->SetLabelSize(0.0);
                projY3->SetStats(0);
                projY3->SetFillStyle(0);
                projY3->Draw("HBAR");
                c->Update(); drawPageTag(++page); c->SaveAs(outputFilenameReport.c_str());
        }

        if (histogramReconstructedCenter2Or3) {
                double percent2 = (graphReconstructedCenters2Or3 && graphReconstructedCenters2Or3->GetN() > 0 && numberOfEntries > 0) ? (100.0 * graphReconstructedCenters2Or3->GetN() / numberOfEntries) : 0.0;
                std::string title2 = !runEnergy.empty() ? runEnergy : ("Run " + runNumber);
                std::string title23 = Form("Tracking (2/3 clusters, 1/detector) [%.1f%% of events] - %s;X [mm];Y [mm]", percent2, title2.c_str());
                histogramReconstructedCenter2Or3->SetTitle(title23.c_str());
                TCanvas *c = new TCanvas("c_recoCenter_2to3", Form("2 or 3 (Run %s)", runNumber.c_str()), 980, 820);
                TPad *padLeft23   = new TPad("padLeft23",   "padLeft23",   0.0, 0.2, 0.2, 1.0);
                TPad *padBottom23 = new TPad("padBottom23", "padBottom23", 0.2, 0.0, 1.0, 0.2);
                TPad *padMain23   = new TPad("padMain23",   "padMain23",   0.2, 0.2, 1.0, 1.0);
                padMain23->SetRightMargin(0.15); padMain23->SetTopMargin(0.16); padMain23->SetBottomMargin(0.12); padMain23->SetLeftMargin(0.12);
                padBottom23->SetTopMargin(0.18); padBottom23->SetBottomMargin(0.35); padBottom23->SetLeftMargin(0.12); padBottom23->SetRightMargin(0.15);
                padLeft23->SetRightMargin(0.05); padLeft23->SetTopMargin(0.05); padLeft23->SetBottomMargin(0.12); padLeft23->SetLeftMargin(0.28);
                c->cd(); padMain23->Draw(); padBottom23->Draw(); padLeft23->Draw();
                // Main 2D plot
                padMain23->cd();
                histogramReconstructedCenter2Or3->GetXaxis()->SetTitle("X [mm]");
                histogramReconstructedCenter2Or3->GetYaxis()->SetTitle("Y [mm]");
                                        // enforce identical axis ranges used for reconstruction so both canvases look identical
                                        // Force the X-axis maximum to 60.0 (user-requested fixed upper limit)
                                        histogramReconstructedCenter2Or3->GetXaxis()->SetRangeUser(centersXMinimum, 60.0);
                                        histogramReconstructedCenter2Or3->GetYaxis()->SetRangeUser(centersYMinimum, centersYMaximum);
                histogramReconstructedCenter2Or3->Draw("COLZ");
                TMarker *centerMarker23 = new TMarker(0.0, 0.0, kFullCircle);
                centerMarker23->SetMarkerColor(kRed); centerMarker23->SetMarkerSize(1.2); centerMarker23->Draw("SAME");
                if (activePolygonPoints.size() >= 3) {
                        auto gpoly = new TGraph((int)activePolygonPoints.size()+1);
                        for (int ip=0; ip<(int)activePolygonPoints.size(); ++ip) gpoly->SetPoint(ip, activePolygonPoints[ip].first, activePolygonPoints[ip].second);
                        gpoly->SetPoint((int)activePolygonPoints.size(), activePolygonPoints[0].first, activePolygonPoints[0].second);
                        gpoly->SetLineColor(kBlack); gpoly->SetLineWidth(2); gpoly->SetFillStyle(0); gpoly->Draw("L SAME");
                        auto leg = new TLegend(0.20, 0.68, 0.78, 0.9);
                        leg->SetBorderSize(0); leg->SetFillStyle(0); leg->SetTextSize(0.028);
                        leg->AddEntry(gpoly, "Active area with all 3 detectors", "l"); leg->Draw();
                }
                // Place stats box
                padMain23->Modified(); padMain23->Update();
                if (auto st = dynamic_cast<TPaveStats*>(histogramReconstructedCenter2Or3->GetListOfFunctions()->FindObject("stats"))) {
                        double x1u = 30.0, x2u = 60.0;
                        double y2u = 70.0 - 5.0;
                        double y1u = y2u - 15.0;
                        auto userToNDC = [&](TPad* pad, double ux, double uy){
                                double uxmin = pad->GetUxmin(); double uxmax = pad->GetUxmax();
                                double uymin = pad->GetUymin(); double uymax = pad->GetUymax();
                                double xndc = pad->GetLeftMargin() + (ux-uxmin)/(uxmax-uxmin) * (1.0 - pad->GetLeftMargin() - pad->GetRightMargin());
                                double yndc = pad->GetBottomMargin() + (uy-uymin)/(uymax-uymin) * (1.0 - pad->GetTopMargin() - pad->GetBottomMargin());
                                return std::pair<double,double>(xndc, yndc);
                        };
                        auto p1 = userToNDC(padMain23, x1u, y1u);
                        auto p2 = userToNDC(padMain23, x2u, y2u);
                        double eps = 0.01;
                        double rightLimit = 1.0 - padMain23->GetRightMargin() - eps;
                        if (p2.first > rightLimit) { double shift = p2.first - rightLimit; p1.first -= shift; p2.first -= shift; }
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
                // Bottom projection X
                padBottom23->cd();
                TH1D *projX23 = histogramReconstructedCenter2Or3->ProjectionX("h_projX_23");
                projX23->SetTitle("");
                projX23->GetXaxis()->SetLabelSize(0.08);
                projX23->GetYaxis()->SetTitle("");
                projX23->GetYaxis()->SetTitleSize(0.08);
                projX23->GetYaxis()->SetLabelSize(0.0);
                projX23->SetStats(0);
                projX23->SetFillStyle(0);
                projX23->Draw("HIST");
                // Left projection Y
                padLeft23->cd();
                TH1D *projY23 = histogramReconstructedCenter2Or3->ProjectionY("h_projY_23");
                projY23->SetTitle("");
                projY23->GetXaxis()->SetTitle("");
                projY23->GetXaxis()->SetTitleSize(0.08);
                projY23->GetXaxis()->SetLabelSize(0.08);
                projY23->GetYaxis()->SetLabelSize(0.0);
                projY23->SetStats(0);
                projY23->SetFillStyle(0);
                projY23->Draw("HBAR");
                c->Update(); drawPageTag(++page); c->SaveAs(outputFilenameReport.c_str());
        }

        // Page 2: Clusters per event per detector
        TCanvas *canvasClustersPerEvent = new TCanvas(Form("c_clustersPerEvent_Run%s", runNumber.c_str()),
                                                                                                Form("Clusters per event (Run %s)", runNumber.c_str()), 800, 600);
        canvasClustersPerEvent->Divide(2, 2);
        canvasClustersPerEvent->cd();
        drawPageTag(++page);

        for (int detectorIndex = 0; detectorIndex < numberOfDetectors; ++detectorIndex) {
                canvasClustersPerEvent->cd(detectorIndex + 1);
                gPad->SetLogy(0);
                histogramClustersPerEvent[detectorIndex]->SetTitle(Form("Clusters per event - D%d (Run %s);Clusters;Events", detectorIndex, runNumber.c_str()));
                histogramClustersPerEvent[detectorIndex]->GetXaxis()->SetRangeUser(0, 6);
                double maxVal = histogramClustersPerEvent[detectorIndex]->GetMaximum();
                if (maxVal > 0) histogramClustersPerEvent[detectorIndex]->SetMaximum(maxVal * 1.25);
                histogramClustersPerEvent[detectorIndex]->Draw();

                // Add percentages
                double totEv = (double)numberOfEntries;
                int nb = histogramClustersPerEvent[detectorIndex]->GetNbinsX();
                TLatex lab;
                lab.SetTextSize(0.035);
                lab.SetTextAlign(21);
                for (int b = 1; b <= nb; ++b) {
                        double val = histogramClustersPerEvent[detectorIndex]->GetBinContent(b);
                        if (val <= 0) continue;
                        double x = histogramClustersPerEvent[detectorIndex]->GetBinCenter(b);
                        double y = val * 1.02;
                        double pct = (totEv > 0.0) ? (100.0 * val / totEv) : 0.0;
                        lab.DrawLatex(x, y, Form("%.1f%%", pct));
                }
        }
        canvasClustersPerEvent->Update();
        canvasClustersPerEvent->SaveAs(outputFilenameReport.c_str());

        // Page 3: Total clusters per event
        TCanvas *canvasTotalClusters = new TCanvas(Form("c_totalClusters_Run%s", runNumber.c_str()),
                                                                                        Form("Total clusters per event (Run %s)", runNumber.c_str()), 800, 600);
        canvasTotalClusters->cd();
        gPad->SetLogy(0);
        histogramTotalClustersPerEvent->SetTitle(Form("Total clusters per event (Run %s);Clusters;Events", runNumber.c_str()));
        histogramTotalClustersPerEvent->Draw();
        drawPageTag(++page);

        canvasTotalClusters->Update();
        canvasTotalClusters->SaveAs(outputFilenameReport.c_str());

        // Page 4: Cluster sizes
        TCanvas *canvasClusterSizes = new TCanvas(Form("c_clusterSizes_Run%s", runNumber.c_str()),
                                                                                        Form("Cluster sizes (Run %s)", runNumber.c_str()), 800, 600);
        canvasClusterSizes->cd();
        gPad->SetLogy(1); // Log scale for sizes
        histogramClusterSizes->SetTitle(Form("Cluster sizes (Run %s);Size (channels);Count", runNumber.c_str()));
        histogramClusterSizes->Draw();
        drawPageTag(++page);

        canvasClusterSizes->Update();
        canvasClusterSizes->SaveAs(outputFilenameReport.c_str());

        // If running in SPS mode, produce only the minimal pages and close the PDF here
        if (spsRunMode) {
                // Close the multi-page PDF and finish
                canvasClusterSizes->SaveAs((outputFilenameReport + ")").c_str());
                LogInfo << "SPS-mode: minimal report generated, closing PDF early." << std::endl;
                inputRootFile->Close();
                LogInfo << "Report generation completed successfully." << std::endl;
                return 0;
        }

        // Page: Cluster amplitudes
        TCanvas *canvasClusterAmplitudes = new TCanvas(Form("c_clusterAmplitudes_Run%s", runNumber.c_str()),
                                                                                                Form("Cluster amplitudes (Run %s)", runNumber.c_str()), 800, 600);
        canvasClusterAmplitudes->cd();
        gPad->SetLogy(1); // Log scale for amplitudes
        histogramClusterAmplitudes->SetTitle(Form("Cluster amplitudes (Run %s);Amplitude;Count", runNumber.c_str()));
        histogramClusterAmplitudes->Draw();
        drawPageTag(++page);

        canvasClusterAmplitudes->Update();
        canvasClusterAmplitudes->SaveAs(outputFilenameReport.c_str());

        // Page: Firing channels 
        TCanvas *canvasChannelsFiring = new TCanvas(Form("c_channelsFiring_Run%s", runNumber.c_str()),
                                                                                        Form("Channels Firing (Run %s)", runNumber.c_str()), 800, 600);
        canvasChannelsFiring->cd();
        gPad->SetLogy(1);
        histogramFiringChannels[0]->SetTitle(Form("Firing channels (Run %s);Channel;Counts", runNumber.c_str()));
        histogramFiringChannels[0]->SetLineColor(detectorColors[0]);
        histogramFiringChannels[0]->Draw();

        TLegend *leg = new TLegend(0.7, 0.7, 0.9, 0.9);
        leg->AddEntry(histogramFiringChannels[0], "D0", "l");

        for (int detectorIndex = 1; detectorIndex < numberOfDetectors; ++detectorIndex) {
                histogramFiringChannels[detectorIndex]->SetLineColor(detectorColors[detectorIndex]);
                histogramFiringChannels[detectorIndex]->Draw("same");
                leg->AddEntry(histogramFiringChannels[detectorIndex], Form("D%d", detectorIndex), "l");
        }
        leg->Draw();

        drawPageTag(++page);

        canvasChannelsFiring->Update();
        canvasChannelsFiring->SaveAs(outputFilenameReport.c_str());


        // Page: Amplitude vs Channel heatmaps (2x2)
        {
                TCanvas *canvasAmplitudeVsChannel = new TCanvas(Form("c_ampVsChannel_Run%s", runNumber.c_str()),
                                                                                        Form("Amplitude vs Channel (Run %s)", runNumber.c_str()), 1200, 900);
                canvasAmplitudeVsChannel->Divide(2,2);
                for (int detectorIndex=0; detectorIndex<numberOfDetectors; ++detectorIndex) {
                        canvasAmplitudeVsChannel->cd(detectorIndex+1);
                        histogramAmplitudeVsChannel[detectorIndex]->Draw("COLZ");
                }
                canvasAmplitudeVsChannel->cd();
                drawPageTag(++page);
                canvasAmplitudeVsChannel->Update();
                // Close the multi-page PDF for non-SPS runs
                canvasAmplitudeVsChannel->SaveAs((outputFilenameReport + ")").c_str());
        }

        LogInfo << "Multi-page PDF report saved successfully: " << outputFilenameReport << std::endl;

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
