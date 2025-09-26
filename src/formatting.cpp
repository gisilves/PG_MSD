#include "TChain.h"
#include "TFile.h"
#include "TError.h"
#include "TH1.h"
#include "TH1I.h"
#include "TF1.h"
#include "TGraph.h"
#include "TAxis.h"
#include "TCanvas.h"
#include "TLatex.h"
#include "TPDF.h"
#include "TKey.h"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <memory>
#include <cctype>

#include "anyoption.h"

namespace {
constexpr size_t kMaxDetectors = 16;
constexpr size_t kChannelsPerDetector = 384;

bool ChannelMask(int channel) {
    int chipNumber = channel / 64;
    (void)chipNumber; // suppress unused warning
    int channelInChip = channel % 64;
    return (channelInChip == 0 || channelInChip == 1 || channelInChip == 62 || channelInChip == 63);
}
}

int main(int argc, char *argv[]) {
    gErrorIgnoreLevel = kWarning;

    AnyOption opt;
    opt.addUsage("Usage: ./formatting [options] input_converted.root output_formatted.root");
    opt.addUsage("");
    opt.addUsage("Options:");
    opt.addUsage("  -h, --help       ................................. Print this help");
    opt.addUsage("  -v, --verbose    ................................. Verbose");
    opt.addUsage("  --cal-file       ................................. Calibration file for baseline subtraction");
    opt.addUsage("  --dune           ................................. DUNE setup (kept for compatibility)");
    opt.addUsage("  --n-sigma        ................................. Sigma threshold for hit counting (default 3)");
    opt.setFlag("help", 'h');
    opt.setFlag("verbose", 'v');
    opt.setFlag("dune");
    opt.setOption("cal-file");
    opt.setOption("n-sigma");

    opt.processCommandArgs(argc, argv);

    if (!opt.hasOptions() || opt.getArgc() < 2) {
        opt.printUsage();
        return 2;
    }

    if (opt.getFlag("help") || opt.getFlag('h')) {
        opt.printUsage();
        return 0;
    }

    bool verbose = opt.getFlag("verbose") || opt.getFlag('v');

    std::string inputFile = opt.getArgv(0);
    std::string outputFile = opt.getArgv(1);

    float hitNSigma = 3.0f;
    if (const char *nSigmaOpt = opt.getValue("n-sigma")) {
        try {
            hitNSigma = std::stof(nSigmaOpt);
        } catch (...) {
            std::cout << "Warning: could not parse --n-sigma value, using default 3" << std::endl;
            hitNSigma = 3.0f;
        }
        if (hitNSigma < 0.0f) hitNSigma = 0.0f;
    }

    // Load calibration if provided
    std::vector<std::vector<float>> baseline(kMaxDetectors, std::vector<float>(kChannelsPerDetector, 0.0f));
    std::vector<std::vector<float>> baselineSigma(kMaxDetectors, std::vector<float>(kChannelsPerDetector, 1.0f));
    bool useCalibration = false;

    if (opt.getValue("cal-file")) {
        std::string calFile = opt.getValue("cal-file");
        std::cout << "Loading calibration from: " << calFile << std::endl;
        std::ifstream cal(calFile);
        if (cal.is_open()) {
            std::string line;
            size_t detIndex = 0;
            size_t channelsRead = 0;
            while (std::getline(cal, line) && detIndex < kMaxDetectors) {
                if (line.empty() || line[0] == '#') continue;
                for (char &c : line) {
                    if (c == ',' || c == '\t') c = ' ';
                }
                std::istringstream iss(line);
                std::vector<float> values;
                float val;
                while (iss >> val) values.push_back(val);

                if (values.size() >= 6) {
                    int ch = static_cast<int>(values[0]);
                    if (ch >= 0 && ch < static_cast<int>(kChannelsPerDetector)) {
                        baseline[detIndex][ch] = values[3];
                        baselineSigma[detIndex][ch] = values[5] > 0.0f ? values[5] : 1.0f;
                        channelsRead++;
                        if (channelsRead == kChannelsPerDetector) {
                            channelsRead = 0;
                            detIndex++;
                        }
                    }
                }
            }
            cal.close();
            useCalibration = true;
            std::cout << "Calibration loaded for " << (useCalibration ? "at least one" : "no") << " detector(s)" << std::endl;
        } else {
            std::cout << "Warning: could not open calibration file, proceeding without baseline subtraction" << std::endl;
        }
    }

    // Open input and output files
    std::unique_ptr<TFile> fin(TFile::Open(inputFile.c_str(), "READ"));
    if (!fin || fin->IsZombie()) {
        std::cout << "Error: cannot open input file " << inputFile << std::endl;
        return 1;
    }

    std::unique_ptr<TFile> fout(TFile::Open(outputFile.c_str(), "RECREATE"));
    if (!fout || fout->IsZombie()) {
        std::cout << "Error: cannot create output file " << outputFile << std::endl;
        return 1;
    }

    // Copy event_info tree if present
    if (TTree *eventInfoIn = static_cast<TTree*>(fin->Get("event_info"))) {
        fout->cd();
        TTree *eventInfoOut = eventInfoIn->CloneTree(-1, "fast");
        if (eventInfoOut) eventInfoOut->Write();
    }

    // Discover detector trees (prefer raw names without underscore if both exist)
    std::map<int, std::string> detectorTreeNames;
    TIter keyIter(fin->GetListOfKeys());
    while (TObject *obj = keyIter()) {
        auto *key = dynamic_cast<TKey*>(obj);
        if (!key) continue;
        if (std::string(key->GetClassName()) != "TTree") continue;
        std::string name = key->GetName();
        if (name == "event_info") continue;
        if (name.rfind("raw_detector", 0) == 0 || name.rfind("sigma", 0) == 0 || name.rfind("pedestal", 0) == 0) continue;

        int detIdx = -1;
        if (name == "raw_events") {
            detIdx = 0;
        } else if (name.rfind("raw_events_", 0) == 0 && !name.empty()) {
            char letter = name.back();
            if (std::isalpha(static_cast<unsigned char>(letter))) {
                detIdx = std::toupper(static_cast<unsigned char>(letter)) - 'A' + 1;
            }
        } else if (name.rfind("detector", 0) == 0) {
            std::string suffix = name.substr(std::string("detector").size());
            if (!suffix.empty() && (suffix[0] == '_' || std::isdigit(suffix[0]))) {
                if (suffix[0] == '_') suffix.erase(0, 1);
                try {
                    detIdx = std::stoi(suffix);
                } catch (...) {
                    detIdx = -1;
                }
            }
        }

        if (detIdx < 0 || detIdx >= static_cast<int>(kMaxDetectors)) continue;

        // Prefer names without underscore when both exist
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
        std::cout << "Error: no detector trees found in input file" << std::endl;
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

    // Prepare containers per detector
    std::vector<TTree*> inputTrees(nDetectors, nullptr);
    std::vector<bool> usesFloat(nDetectors, false);
    std::vector<std::vector<float>*> floatBuffers(nDetectors, nullptr);
    std::vector<std::vector<unsigned int>*> uintBuffers(nDetectors, nullptr);

    std::vector<TTree*> dataTreesOut(nDetectors, nullptr);
    std::vector<std::vector<float>*> dataVectorsOut(nDetectors, nullptr);
    std::vector<TTree*> rawTreesOut(nDetectors, nullptr);
    std::vector<std::vector<float>*> rawVectorsOut(nDetectors, nullptr);
    std::vector<TH1I*> firingHists(nDetectors, nullptr);
    std::vector<TH1I*> hitsHists(nDetectors, nullptr);

    // Configure branch addresses
    for (size_t idx = 0; idx < nDetectors; ++idx) {
        int detIdx = detectorIndices[idx];
        const std::string &treeName = detectorNames[idx];
        inputTrees[idx] = static_cast<TTree*>(fin->Get(treeName.c_str()));
        if (!inputTrees[idx]) {
            std::cout << "Warning: could not retrieve tree " << treeName << std::endl;
            continue;
        }

        std::vector<std::string> branchCandidates = {"data", "RAW Event", "RAW Event J5", "RAW Event J7"};
        bool branchFound = false;
        for (const auto &branchName : branchCandidates) {
            if (inputTrees[idx]->GetBranch(branchName.c_str())) {
                if (branchName == "data") {
                    usesFloat[idx] = true;
                    inputTrees[idx]->SetBranchAddress(branchName.c_str(), &floatBuffers[idx]);
                } else {
                    usesFloat[idx] = false;
                    inputTrees[idx]->SetBranchAddress(branchName.c_str(), &uintBuffers[idx]);
                }
                branchFound = true;
                break;
            }
        }

        if (!branchFound) {
            std::cout << "Warning: detector " << detIdx << " has no recognised data branch, skipping" << std::endl;
            inputTrees[idx] = nullptr;
            continue;
        }

        // Prepare output objects for this detector
        firingHists[idx] = new TH1I(Form("h_firingChannels_D%d", detIdx),
                                    Form("Firing Channels Detector %d", detIdx),
                                    kChannelsPerDetector, 0, kChannelsPerDetector);
        hitsHists[idx] = new TH1I(Form("h_hitsPerEvent_D%d", detIdx),
                                   Form("Hits per Event Detector %d", detIdx),
                                   200, 0, 200);

        dataTreesOut[idx] = new TTree(Form("detector%d", detIdx),
                                      Form("Baseline-subtracted data detector %d", detIdx));
        dataVectorsOut[idx] = new std::vector<float>();
        dataTreesOut[idx]->Branch("data", &dataVectorsOut[idx]);

        rawTreesOut[idx] = new TTree(Form("raw_detector%d", detIdx),
                                     Form("Raw data detector %d", detIdx));
        rawVectorsOut[idx] = new std::vector<float>();
        rawTreesOut[idx]->Branch("raw_data", &rawVectorsOut[idx]);

        // Write pedestal and sigma trees (single entry)
        fout->cd();
        auto *pedestalVec = new std::vector<float>(baseline[detIdx]);
        auto *sigmaVec = new std::vector<float>(baselineSigma[detIdx]);
        TTree pedestalTree(Form("pedestal%d", detIdx), Form("Pedestals detector %d", detIdx));
        pedestalTree.Branch("pedestal", &pedestalVec);
        pedestalTree.Fill();
        pedestalTree.Write();
        TTree sigmaTree(Form("sigma%d", detIdx), Form("Sigma detector %d", detIdx));
        sigmaTree.Branch("sigma", &sigmaVec);
        sigmaTree.Fill();
        sigmaTree.Write();
        delete pedestalVec;
        delete sigmaVec;
    }

    // Process events for each detector
    for (size_t idx = 0; idx < nDetectors; ++idx) {
        if (!inputTrees[idx]) continue;

        int detIdx = detectorIndices[idx];
        if (verbose) {
            std::cout << "Formatting detector " << detIdx << " using tree " << detectorNames[idx] << std::endl;
        }

        Long64_t nEntries = inputTrees[idx]->GetEntries();
        for (Long64_t entry = 0; entry < nEntries; ++entry) {
            inputTrees[idx]->GetEntry(entry);

            const std::vector<float> *floatData = floatBuffers[idx];
            const std::vector<unsigned int> *uintData = uintBuffers[idx];
            if (usesFloat[idx] && !floatData) continue;
            if (!usesFloat[idx] && !uintData) continue;

            size_t channelCount = usesFloat[idx] ? floatData->size() : uintData->size();

            rawVectorsOut[idx]->assign(channelCount, 0.0f);
            dataVectorsOut[idx]->assign(channelCount, 0.0f);

            int hitsInEvent = 0;

            for (size_t ch = 0; ch < channelCount; ++ch) {
                float adc = usesFloat[idx] ? (*floatData)[ch] : static_cast<float>((*uintData)[ch]);
                (*rawVectorsOut[idx])[ch] = adc;

                float pedestal = 0.0f;
                float sigma = 1.0f;
                if (detIdx < static_cast<int>(baseline.size()) && ch < baseline[detIdx].size()) {
                    pedestal = baseline[detIdx][ch];
                    sigma = baselineSigma[detIdx][ch];
                }
                if (!useCalibration) {
                    pedestal = 0.0f;
                    sigma = 1.0f;
                }
                if (sigma <= 0.0f) sigma = 1.0f;

                float calibrated = adc - pedestal;
                (*dataVectorsOut[idx])[ch] = calibrated;

                if (useCalibration && !ChannelMask(static_cast<int>(ch))) {
                    if (calibrated > hitNSigma * sigma) {
                        firingHists[idx]->Fill(ch);
                        hitsInEvent++;
                    }
                }
            }

            if (useCalibration && hitsInEvent > 0) {
                hitsHists[idx]->Fill(hitsInEvent);
            }

            dataTreesOut[idx]->Fill();
            rawTreesOut[idx]->Fill();
        }

        fout->cd();
        dataTreesOut[idx]->Write();
        rawTreesOut[idx]->Write();
        if (firingHists[idx]) firingHists[idx]->Write();
        if (hitsHists[idx]) hitsHists[idx]->Write();
    }

    // Clean up allocated vectors
    for (size_t idx = 0; idx < nDetectors; ++idx) {
        delete dataVectorsOut[idx];
        delete rawVectorsOut[idx];
        delete firingHists[idx];
        delete hitsHists[idx];
    }

    fout->Close();
    fin->Close();

    std::cout << "Formatting complete. Output saved to " << outputFile << std::endl;
    return 0;
}