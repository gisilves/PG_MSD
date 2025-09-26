#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <cmath>
#include <memory>

#include "TFile.h"
#include "TTree.h"
#include "TBranch.h"
#include "TKey.h"
#include "TH1.h"

#include "Logger.h"
#include "CmdLineParser.h"

LoggerInit([] {
    Logger::getUserHeader() << "[" << FILENAME << "]";
});

namespace {
constexpr size_t kMaxDetectors = 16;
constexpr size_t kChannelsPerDetector = 384;

bool ChannelMask(int channel) {
    int channelInChip = channel % 64;
    return (channelInChip == 0 || channelInChip == 1 || channelInChip == 62 || channelInChip == 63);
}
}

int main(int argc, char *argv[]) {
    CmdLineParser clp;

    clp.getDescription() << "> This program reads a formatted ROOT file and performs clustering." << std::endl;

    clp.addDummyOption("Main options");
    clp.addOption("inputRootFile", {"-i", "--input"}, "Input formatted ROOT file");
    clp.addOption("outputRootFile", {"-o", "--output"}, "Output ROOT file with clusters");
    clp.addOption("nSigma", {"-s", "--n-sigma"}, "Sigma threshold for hit finding (default 5)");

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
    std::string outputFile = clp.getOptionVal<std::string>("outputRootFile");

    float nSigmaThreshold = 5.0f;
    if (clp.isOptionTriggered("nSigma")) {
        nSigmaThreshold = clp.getOptionVal<float>("nSigma");
        if (nSigmaThreshold < 0.0f) nSigmaThreshold = 0.0f;
    }

    LogInfo << "Input file: " << inputFile << std::endl;
    LogInfo << "Output file: " << outputFile << std::endl;
    LogInfo << "Using nSigma threshold: " << nSigmaThreshold << std::endl;

    std::unique_ptr<TFile> inputRootFile(TFile::Open(inputFile.c_str(), "READ"));
    if (!inputRootFile || inputRootFile->IsZombie()) {
        LogError << "Error: cannot open input file " << inputFile << std::endl;
        return 1;
    }

    // Discover detector trees
    std::map<int, std::string> detectorTreeNames;
    TIter keyIter(inputRootFile->GetListOfKeys());
    while (TObject *obj = keyIter()) {
        auto *key = dynamic_cast<TKey*>(obj);
        if (!key) continue;
        if (std::string(key->GetClassName()) != "TTree") continue;
        std::string name = key->GetName();
        if (name == "event_info" || name.rfind("raw_detector", 0) == 0 || name.rfind("sigma", 0) == 0 || name.rfind("pedestal", 0) == 0) continue;

        int detIdx = -1;
        if (name.rfind("detector", 0) == 0) {
            std::string suffix = name.substr(std::string("detector").size());
            if (!suffix.empty() && suffix[0] == '_') suffix.erase(0, 1);
            try {
                detIdx = std::stoi(suffix);
            } catch (...) {
                detIdx = -1;
            }
        }

        if (detIdx < 0 || detIdx >= static_cast<int>(kMaxDetectors)) continue;

        auto existing = detectorTreeNames.find(detIdx);
        if (existing == detectorTreeNames.end()) {
            detectorTreeNames[detIdx] = name;
        } else {
            bool existingHasUnderscore = existing->second.find("_") != std::string::npos;
            bool newHasUnderscore = name.find("_") != std::string::npos;
            if (existingHasUnderscore && !newHasUnderscore) {
                detectorTreeNames[detIdx] = name;
            }
        }
    }

    if (detectorTreeNames.empty()) {
        LogError << "Error: no detector trees found in formatted file" << std::endl;
        return 1;
    }

    std::vector<int> detectorIndices;
    std::vector<std::string> detectorNames;
    detectorIndices.reserve(detectorTreeNames.size());
    detectorNames.reserve(detectorTreeNames.size());
    for (const auto &kv : detectorTreeNames) {
        detectorIndices.push_back(kv.first);
        detectorNames.push_back(kv.second);
    }

    size_t nDetectors = detectorIndices.size();

    std::vector<TTree*> detectorTrees(nDetectors, nullptr);
    std::vector<std::vector<float>*> detectorData(nDetectors, nullptr);
    std::vector<bool> detectorBranchValid(nDetectors, false);

    std::vector<std::vector<float>> sigmaValues(nDetectors, std::vector<float>(kChannelsPerDetector, 1.0f));
    std::vector<std::vector<float>> pedestalValues(nDetectors, std::vector<float>(kChannelsPerDetector, 0.0f));

    for (size_t idx = 0; idx < nDetectors; ++idx) {
        detectorTrees[idx] = static_cast<TTree*>(inputRootFile->Get(detectorNames[idx].c_str()));
        if (!detectorTrees[idx]) {
            LogError << "Error: cannot access detector tree " << detectorNames[idx] << std::endl;
            return 1;
        }

        detectorData[idx] = nullptr;
        if (detectorTrees[idx]->GetBranch("data")) {
            detectorTrees[idx]->SetBranchAddress("data", &detectorData[idx]);
            detectorBranchValid[idx] = true;
        } else if (detectorTrees[idx]->GetBranch("RAW Event")) {
            // Support legacy naming where data is stored as unsigned ints
            static std::vector<std::vector<unsigned int>*> legacyBuffers;
            legacyBuffers.resize(nDetectors, nullptr);
            legacyBuffers[idx] = nullptr;
            detectorTrees[idx]->SetBranchAddress("RAW Event", &legacyBuffers[idx]);
            detectorBranchValid[idx] = true;
        } else {
            LogError << "Error: detector tree " << detectorNames[idx] << " has no recognised branch" << std::endl;
            return 1;
        }

        int detIdx = detectorIndices[idx];

        if (TTree *sigmaTree = static_cast<TTree*>(inputRootFile->Get(Form("sigma%d", detIdx)))) {
            std::vector<float> *sigmaVec = nullptr;
            sigmaTree->SetBranchAddress("sigma", &sigmaVec);
            sigmaTree->GetEntry(0);
            if (sigmaVec) sigmaValues[idx] = *sigmaVec;
        }

        if (TTree *pedestalTree = static_cast<TTree*>(inputRootFile->Get(Form("pedestal%d", detIdx)))) {
            std::vector<float> *pedVec = nullptr;
            pedestalTree->SetBranchAddress("pedestal", &pedVec);
            pedestalTree->GetEntry(0);
            if (pedVec) pedestalValues[idx] = *pedVec;
        }
    }

    TTree *eventInfoTree = static_cast<TTree*>(inputRootFile->Get("event_info"));
    if (!eventInfoTree) {
        LogError << "Error: cannot find event_info tree" << std::endl;
        return 1;
    }

    Long64_t evt_size = 0, fw_version = 0, trigger_number = 0, board_id = 0, timestamp = 0, ext_timestamp = 0, trigger_id = 0, file_offset = 0;
    Int_t event_index = 0;
    eventInfoTree->SetBranchAddress("event_index", &event_index);
    eventInfoTree->SetBranchAddress("evt_size", &evt_size);
    eventInfoTree->SetBranchAddress("fw_version", &fw_version);
    eventInfoTree->SetBranchAddress("trigger_number", &trigger_number);
    eventInfoTree->SetBranchAddress("board_id", &board_id);
    eventInfoTree->SetBranchAddress("timestamp", &timestamp);
    eventInfoTree->SetBranchAddress("ext_timestamp", &ext_timestamp);
    eventInfoTree->SetBranchAddress("trigger_id", &trigger_id);
    eventInfoTree->SetBranchAddress("file_offset", &file_offset);

    Long64_t nEntries = detectorTrees[0]->GetEntries();
    LogInfo << "Number of events: " << nEntries << std::endl;

    std::unique_ptr<TFile> outputRootFile(TFile::Open(outputFile.c_str(), "RECREATE"));
    if (!outputRootFile || outputRootFile->IsZombie()) {
        LogError << "Error: cannot create output file " << outputFile << std::endl;
        return 1;
    }

    auto *cluster_detectors = new std::vector<int>();
    auto *cluster_start_ch = new std::vector<int>();
    auto *cluster_end_ch = new std::vector<int>();
    auto *cluster_sizes = new std::vector<int>();
    auto *cluster_amplitudes = new std::vector<float>();

    TTree *clusterTree = new TTree("clusters", "Clusters per event");
    clusterTree->Branch("detector", &cluster_detectors);
    clusterTree->Branch("start_ch", &cluster_start_ch);
    clusterTree->Branch("end_ch", &cluster_end_ch);
    clusterTree->Branch("size", &cluster_sizes);
    clusterTree->Branch("amplitude", &cluster_amplitudes);

    TTree *outEventInfo = eventInfoTree->CloneTree(0);
    outEventInfo->SetName("event_info");

    std::vector<TTree*> outDetectorTrees(nDetectors, nullptr);
    std::vector<std::vector<float>*> outDetectorData(nDetectors, nullptr);
    for (size_t idx = 0; idx < nDetectors; ++idx) {
        int detIdx = detectorIndices[idx];
        outDetectorTrees[idx] = new TTree(Form("detector%d", detIdx), Form("Hits detector %d", detIdx));
        outDetectorData[idx] = new std::vector<float>();
        outDetectorTrees[idx]->Branch("data", &outDetectorData[idx]);
    }

    for (Long64_t entry = 0; entry < nEntries; ++entry) {
        if (verbose && entry % 100 == 0) {
            LogInfo << "Processing event " << entry << std::endl;
        }

        for (size_t idx = 0; idx < nDetectors; ++idx) {
            detectorTrees[idx]->GetEntry(entry);
        }
        eventInfoTree->GetEntry(entry);

        cluster_detectors->clear();
        cluster_start_ch->clear();
        cluster_end_ch->clear();
        cluster_sizes->clear();
        cluster_amplitudes->clear();

        for (size_t idx = 0; idx < nDetectors; ++idx) {
            if (!detectorBranchValid[idx]) continue;

            const std::vector<float> *dataVec = detectorData[idx];
            if (!dataVec) {
                // Branch may be stored as unsigned ints (legacy). Fetch through tree->GetBranch() if needed.
                continue;
            }

            size_t channelCount = dataVec->size();
            outDetectorData[idx]->assign(channelCount, 0.0f);

            std::vector<int> hitChannels;
            for (size_t ch = 0; ch < channelCount; ++ch) {
                float sigma = (ch < sigmaValues[idx].size()) ? sigmaValues[idx][ch] : 1.0f;
                if (sigma <= 0.0f) sigma = 1.0f;
                float value = (*dataVec)[ch];

                bool isHit = !ChannelMask(static_cast<int>(ch)) && (value > nSigmaThreshold * sigma);
                if (isHit) {
                    hitChannels.push_back(static_cast<int>(ch));
                    (*outDetectorData[idx])[ch] = value;
                }
            }

            if (!hitChannels.empty()) {
                int start = hitChannels.front();
                int prev = hitChannels.front();
                float amplitude = (*outDetectorData[idx])[start];

                for (size_t i = 1; i < hitChannels.size(); ++i) {
                    int ch = hitChannels[i];
                    if (ch != prev + 1) {
                        cluster_detectors->push_back(detectorIndices[idx]);
                        cluster_start_ch->push_back(start);
                        cluster_end_ch->push_back(prev);
                        cluster_sizes->push_back(prev - start + 1);
                        cluster_amplitudes->push_back(amplitude);
                        start = ch;
                        amplitude = (*outDetectorData[idx])[ch];
                    } else {
                        amplitude += (*outDetectorData[idx])[ch];
                    }
                    prev = ch;
                }

                cluster_detectors->push_back(detectorIndices[idx]);
                cluster_start_ch->push_back(start);
                cluster_end_ch->push_back(prev);
                cluster_sizes->push_back(prev - start + 1);
                cluster_amplitudes->push_back(amplitude);

                if (verbose && entry < 5) {
                    LogInfo << "Detector " << detectorIndices[idx] << ": " << hitChannels.size()
                            << " hit channels -> " << cluster_sizes->back() << " sized last cluster" << std::endl;
                }
            }
        }

        clusterTree->Fill();
        outEventInfo->Fill();
        for (size_t idx = 0; idx < nDetectors; ++idx) {
            outDetectorTrees[idx]->Fill();
        }
    }

    outputRootFile->cd();
    clusterTree->Write();
    outEventInfo->Write();
    for (size_t idx = 0; idx < nDetectors; ++idx) {
        outDetectorTrees[idx]->Write();
    }

    // Copy sigma, pedestals, raw data, and diagnostic histograms from input file
    for (size_t idx = 0; idx < nDetectors; ++idx) {
        int detIdx = detectorIndices[idx];
        if (auto *sigmaTree = static_cast<TTree*>(inputRootFile->Get(Form("sigma%d", detIdx)))) {
            auto *clone = sigmaTree->CloneTree(-1, "fast");
            if (clone) clone->Write();
        }
        if (auto *pedestalTree = static_cast<TTree*>(inputRootFile->Get(Form("pedestal%d", detIdx)))) {
            auto *clone = pedestalTree->CloneTree(-1, "fast");
            if (clone) clone->Write();
        }
        if (auto *rawTree = static_cast<TTree*>(inputRootFile->Get(Form("raw_detector%d", detIdx)))) {
            auto *clone = rawTree->CloneTree(-1, "fast");
            if (clone) clone->Write();
        }
        if (auto *hist = dynamic_cast<TH1*>(inputRootFile->Get(Form("h_firingChannels_D%d", detIdx)))) {
            auto *histClone = dynamic_cast<TH1*>(hist->Clone());
            if (histClone) {
                histClone->Write();
                delete histClone;
            }
        }
        if (auto *hist = dynamic_cast<TH1*>(inputRootFile->Get(Form("h_hitsPerEvent_D%d", detIdx)))) {
            auto *histClone = dynamic_cast<TH1*>(hist->Clone());
            if (histClone) {
                histClone->Write();
                delete histClone;
            }
        }
    }

    outputRootFile->Close();
    inputRootFile->Close();

    delete cluster_detectors;
    delete cluster_start_ch;
    delete cluster_end_ch;
    delete cluster_sizes;
    delete cluster_amplitudes;

    for (size_t idx = 0; idx < nDetectors; ++idx) {
        delete outDetectorData[idx];
    }

    LogInfo << "Clustering completed. Output written to " << outputFile << std::endl;
    return 0;
}
