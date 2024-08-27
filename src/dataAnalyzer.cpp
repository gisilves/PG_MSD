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


    ///////////////////////////
    // Some variables

    int nDetectors = 4;
    int nChannels = 384;

    ///////////////////////////

    // get calibration file
    std::string inputCalFile = clp.getOptionVal<std::string>("inputCalFile");
    std::ifstream calFile(inputCalFile);
    // check if the file is open correctly
    if (!calFile.is_open()) {
        std::cout << "Error: calibration file not open" << std::endl;
        return 1;
    }

    // the format is // TODO move to some docs
    // 0: channel
    // 1: channel / 64
    // 2: va_chan
    // 3: pedestals->at(ch)
    // 4: rsigma->at(ch)
    // 5: sigma_value
    // 6: badchan
    // 7: 0.000

    /// Calib file
    // read the calibration file and store the values in a vector
    std::vector <std::vector <float>> *baseline;
    baseline->reserve(nDetectors); // detectors
    for (int i = 0; i < nDetectors; i++)  baseline->at(i).reserve(nChannels); // channels per detector
    

    std::vector <std::vector <float>> *baseline_sigma; // not the raw one
    baseline_sigma->reserve(nDetectors); // detectors
    for (int i = 0; i < nDetectors; i++) baseline_sigma->at(i).reserve(nChannels); // channels per detector


    // skip header lines, starting with #
    std::string line;
    while (std::getline(calFile, line)) {
        
        if (line[0] == '#') continue; // header
        
        // values are separated by commas, read all and store line by line
        std::istringstream iss(line);
        std::vector <float> values;
        float value;
        while (iss >> value) {
            values.push_back(value);
        }

        // check if the values are correct
        if (values.size() != 8) {
            std::cout << "Error: wrong number of values in the calibration file" << std::endl;
            return 1;
        }

        // store the values
        baseline->at(values.at(1)).at(values.at(0)) = values.at(3);
        baseline_sigma->at(values.at(1)).at(values.at(0)) = values.at(5);

    }

    /// ROOT file

    // Get root file name
    std::string input_root_filename = clp.getOptionVal<std::string>("inputRootFile");
    TFile *input_root_file = new TFile(input_root_filename.c_str(), "READ");
    // check if the file is open correctly
    if (!input_root_file->IsOpen()) {
        LogError << "Error: file not open" << std::endl;
        return 1;
    }

    // get trees TODO change at converter level, I don't like the names
    std::vector <TTree*> raw_events_trees;
    raw_events_trees.reserve(nDetectors);
    raw_events_trees.at(0) = (TTree*)input_root_file->Get("raw_events");
    raw_events_trees.at(1) = (TTree*)input_root_file->Get("raw_events_B");
    raw_events_trees.at(2) = (TTree*)input_root_file->Get("raw_events_C");
    raw_events_trees.at(3) = (TTree*)input_root_file->Get("raw_events_D");

    std::vector <TBranch*> raw_events_branches;
    raw_events_branches.reserve(nDetectors);
    raw_events_branches.at(0) = raw_events_trees.at(0)->GetBranch("RAW Event");
    raw_events_branches.at(1) = raw_events_trees.at(1)->GetBranch("RAW Event B");
    raw_events_branches.at(2) = raw_events_trees.at(2)->GetBranch("RAW Event C");
    raw_events_branches.at(3) = raw_events_trees.at(3)->GetBranch("RAW Event D");

    // get the number of entries
    std::vector <int> nEntries;
    for (int i = 0; i < nDetectors; i++) {
        nEntries.at(i) = raw_events_trees.at(i)->GetEntries();
        LogInfo << "Detector " << i << " has " << nEntries.at(i) << " entries" << std::endl;
    }

    // Entries should always be the same for all detectors
    if (nEntries.at(0) != nEntries.at(1) || nEntries.at(0) != nEntries.at(2) || nEntries.at(0) != nEntries.at(3)) {
        LogError << "Error: number of entries is different for the detectors! Something went wrong" << std::endl;
        return 1;
    }

    // each branch has a vector, that corresponds to the list of channels
    // let's have the concept of event. Will go in a class

    class Event {
        public:
            
            // variables
            int nDetectors = 4; // TODO improve this
            int nChannels = 384; // TODO improve this
            std::vector <std::vector <float>> *peak; // could be int?
            std::vector <std::vector <float>> *baseline; 
            std::vector <std::vector <float>> *sigma;

            // constructor and destructor
            Event() {
                peak = new std::vector <std::vector <float>>;
                peak->reserve(nDetectors);
                baseline = new std::vector <std::vector <float>>;
                baseline->reserve(nDetectors);
                sigma = new std::vector <std::vector <float>>;
                sigma->reserve(nDetectors);
                for (int i = 0; i < nDetectors; i++) {
                    peak->at(i).reserve(nChannels);
                    baseline->at(i).reserve(nChannels);
                    sigma->at(i).reserve(nChannels);
                }
            }
            ~Event() {delete peak;}

            // methods
            void AddPeak(std::vector <float> *_peak_oneDetector) {peak->emplace_back(_peak_oneDetector);} // one vector per detector

            void AddBaseline(std::vector <std::vector <float>> * _baseline) {baseline = _baseline;} // same throughout the run, see if good assumption

            void AddSigma(std::vector <std::vector <float>> * _sigma) {sigma = _sigma;} // same throughout the run, see if good assumption
    };
    

    // loop over the entries to get the peak. For each entry, store the values in the event

    std::vector <Event> events;
    events.reserve(nEntries.at(0)); // should all be the same
    std::vector <float> *data0 = new std::vector <float>;
    // std::vector <float> *data1 = new std::vector <float>;
    // std::vector <float> *data2 = new std::vector <float>;
    // std::vector <float> *data3 = new std::vector <float>;
    raw_events_branches.at(0)->SetAddress(&data0);
    // raw_events_branches.at(1)->SetAddress(&data1);
    // raw_events_branches.at(2)->SetAddress(&data2);
    // raw_events_branches.at(3)->SetAddress(&data3);

    for (int i = 0; i < nEntries.at(0); i++) {

        Event event; // across detectors
        event.AddBaseline(baseline);
        event.AddSigma(baseline_sigma); 

        data0->clear();
        raw_events_branches.at(0)->GetEntry(i);
        event.AddPeak(data0);
        events.emplace_back(event);

        // for (int j = 0; j < nDetectors; j++) {
        //     data->clear();
        //     raw_events_branches.at(j)->GetEntry(i);
        //     event.AddPeak(data);
        //     events.emplace_back(event);
        // }
    }

    // print values minus baseline
    for (int i = 0; i < nEntries.at(0); i++) {
        for (int j = 0; j < nChannels; j++) {
            std::cout << "Entry " << i << " has a value of " << events.at(i).peak->at(0).at(j) - events.at(i).baseline->at(0).at(j) << " at position " << j << std::endl;
        }
    }


    // print if the value is above threshold
    // int threshold = 600;
    // for (int i = 0; i < nentries; i++) {
    //     branch->GetEntry(i);
    //     for (int j = 129; j < 191; j++) {
    //         if (data->at(j) > threshold) {
    //             std::cout << "Entry " << i << " has a value above " << threshold << " at position " << j << std::endl;
    //         }
    //     }
    // }

    return 0;
}
