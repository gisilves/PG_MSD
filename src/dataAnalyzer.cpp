///////////////////////////////////////
// Created by E. Villa on 2024-08-27.//
// Code to look at the data of the   //
// NP02 beam monitor.                //
///////////////////////////////////////

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#include "TFile.h"
#include "TTree.h"
#include "TBranch.h"
#include "TH1F.h"
#include "TH2F.h"
#include "TCanvas.h"
#include "TGraph.h"

#include "CmdLineParser.h"
#include "Logger.h"

LoggerInit([]{
  Logger::getUserHeader() << "[" << FILENAME << "]";
});

int main(int argc, char* argv[]) {

    CmdLineParser clp;

    clp.getDescription() << "> This program takes a root file and a calibration file and analyzes it." << std::endl;

    clp.addDummyOption("Main options");
    // clp.addOption("runNumber", {"-n", "--run-number"}, "Specify run number.");
    clp.addOption("appSettings",    {"-s", "--app-settings"},   "Specify application settings file path.");
    clp.addOption("inputRootFile",  {"-r", "--root-file"},      "Root converted data.");
    clp.addOption("inputCalFile",   {"-c", "--cal-file"},       "Calibration file.");
    clp.addOption("nDetectors",     {"-n", "--n-detectors"},     "Number of detectors installed.");
    clp.addOption("outputDir",      {"-o", "--output"},         "Specify output directory path");

    clp.addDummyOption("Triggers");
    clp.addTriggerOption("verboseMode", {"-v"}, "RunVerboseMode, bool");

    clp.addDummyOption();

    // usage always displayed
    LogInfo << clp.getDescription().str() << std::endl;

    LogInfo << "Usage: " << std::endl;
    LogInfo << clp.getConfigSummary() << std::endl << std::endl;

    clp.parseCmdLine(argc, argv);

    LogThrowIf( clp.isNoOptionTriggered(), "No option was provided." );

    LogInfo << "Provided arguments: " << std::endl;
    LogInfo << clp.getValueSummary() << std::endl << std::endl;

    // get calibration file
    std::string inputCalFile = clp.getOptionVal<std::string>("inputCalFile");
    std::ifstream calFile(inputCalFile);
    // check if the file is open correctly
    if (!calFile.is_open()) {
        std::cout << "Error: calibration file not open" << std::endl;
        return 1;
    }

    // read the calibration file and store the values in a vector
    std::vector <vector <float>> baseline;
    baseline.reserve(1); // one for each detector, for now default
    baseline.at(0).reserve(384); // channels per detector

    std::vector <vector <float>> baseline_sigma;
    baseline_sigma.reserve(1); // one for each detector, for now default
    baseline_sigma.at(0).reserve(384); // channels per detector

    // get file name
    std::string inputRootFile = clp.getOptionVal<std::string>("inputRootFile");
    TFile *file = new TFile(inputRootFile.c_str(), "READ");
    // check if the file is open correctly
    if (!file->IsOpen()) {
        std::cout << "Error: file not open" << std::endl;
        return 1;
    }

    // get first tree, called raw_event
    TTree *raw_event = (TTree*)file->Get("raw_events");
    // get the number of entries
    int nentries = raw_event->GetEntries();
    std::cout << "Number of entries: " << nentries << std::endl;

    // get the branch, called "RAW Event"
    TBranch *branch = raw_event->GetBranch("RAW Event"); // don't like the space

    // create a vector of unsigned integers, that is the content of the branch
    std::vector<unsigned int> *data = new std::vector<unsigned int>;
    branch->SetAddress(&data);

    // loop over the entries
    for (int i = 0; i < nentries; i++) {
        branch->GetEntry(i);
        // std::cout << "Entry " << i << " has " << data->size() << " words" << std::endl;
    }

    // print if the value is above threshold
    int threshold = 600;
    for (int i = 0; i < nentries; i++) {
        branch->GetEntry(i);
        for (int j = 129; j < 191; j++) {
            if (data->at(j) > threshold) {
                std::cout << "Entry " << i << " has a value above " << threshold << " at position " << j << std::endl;
            }
        }
    }

    return 0;
}
