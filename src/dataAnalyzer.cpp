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
#include "TApplication.h"

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
    clp.addOption("inputRootFile",  {"-r", "--root-file"},      "Root converted data");
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

    bool verbose = clp.isOptionTriggered("verboseMode");

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
    if (!input_root_file->IsOpen()) {
        LogError << "Error: file not open" << std::endl;
        return 1;
    }

    // get trees
    // TODO change at converter level, I don't like the names
    std::vector <TTree*> raw_events_trees = std::vector <TTree*>();
    raw_events_trees.reserve(nDetectors);
    TTree *these_raw_events = (TTree*)input_root_file->Get("raw_events");
    raw_events_trees.emplace_back(these_raw_events);
    these_raw_events = (TTree*)input_root_file->Get("raw_events_B");
    raw_events_trees.emplace_back(these_raw_events);
    these_raw_events = (TTree*)input_root_file->Get("raw_events_C");
    raw_events_trees.emplace_back(these_raw_events);
    these_raw_events = (TTree*)input_root_file->Get("raw_events_D");
    raw_events_trees.emplace_back(these_raw_events);

    LogInfo << "Got the trees" << std::endl;

    // get the number of entries
    std::vector <int> nEntries = std::vector <int>();
    nEntries.reserve(nDetectors);
    for (int detit = 0; detit < nDetectors; detit++) {
        nEntries.emplace_back(raw_events_trees.at(detit)->GetEntries());
        LogInfo << "Detector " << detit << " has " << nEntries.at(detit) << " entries" << std::endl;
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
            std::vector <std::vector <float>*> *peak; // could be int?
            std::vector <std::vector <float>> baseline {};
            std::vector <std::vector <float>> sigma {};

            // save triggered hits as vector of pairs det and ch
            std::vector <std::pair<int, int>> *triggeredHits;
            bool extractedTriggeredHits = false;

            int nsigma = 8; // to consider something a valid hit

            // constructor and destructor
            Event() {
                peak = new std::vector <std::vector <float>*>;
                peak->reserve(nDetectors);
                // baseline = new std::vector <std::vector<float>*>;
                // baseline.reserve(nDetectors);
                // sigma = new std::vector <std::vector<float>*>;
                // sigma->reserve(nDetectors);
                triggeredHits = new std::vector <std::pair<int, int>>;
                triggeredHits->reserve(nChannels*nDetectors); //  potential maximum number of hits, memory is not a problem for now
                // for (int i = 0; i < nDetectors; i++) {
                //     peak->emplace_back(new std::vector<float>);
                //     peak->at(i)->reserve(nChannels);
                //     baseline.emplace_back(new std::vector<float>);
                //     baseline.at(i)->reserve(nChannels);
                //     sigma->emplace_back(new std::vector<float>);
                //     sigma->at(i)->reserve(nChannels);
                // }

            }

            ~Event() {delete peak; delete triggeredHits;}

            // setters
            void SetBaseline(std::vector <std::vector <float>> _baseline) {baseline = _baseline;}
            void SetSigma(std::vector <std::vector <float>> _sigma) {sigma = _sigma;}
            void SetNsigma(int _nsigma) {nsigma = _nsigma;}
            void SetPeak(std::vector <std::vector <float>*> * _peak) {peak = _peak;}

            // getters
            std::vector <std::vector <float>*> * GetPeak() {return peak;}
            std::vector <float> * GetPeak(int _det) {return peak->at(_det);}
            float GetPeak(int _det, int _channel) {return peak->at(_det)->at(_channel);}
            std::vector <std::vector <float>> GetBaseline() {return baseline;}
            float GetBaseline(int _det, int _channel) {return baseline.at(_det).at(_channel);}
            std::vector <std::vector <float>> GetSigma() {return sigma;}
            float GetSigma(int _det, int _channel) {return sigma.at(_det).at(_channel);}
            int GetNsigma() {return nsigma;}
            std::vector <std::pair<int, int>> * GetTriggeredHits() {return triggeredHits;}

            // methods 
            // TODO this should have a check that the size of peak is < nDetectors
            void AddPeak(int _detector_number, std::vector <float> * _peak_oneDetector) {
                peak->emplace(peak->begin() + _detector_number, _peak_oneDetector);
            } // one vector per detector

            void ExtractTriggeredHits() {
                // loop over all the peaks in here and store the ones that are above nsigma*sigma
                for (int detit = 0; detit < nDetectors; detit++) {
                    for (int chit = 0; chit < nChannels; chit++) {                        
                        // LogInfo << "peak is " << peak->at(detit)->at(chit) << std::endl;
                        // LogInfo << "baseline is " << baseline.at(detit).at(chit) << std::endl;
                        // LogInfo << "sigma is " << sigma->at(detit)->at(chit) << std::endl;
                        if (GetPeak(detit, chit) - GetBaseline(detit, chit) > nsigma * GetSigma(detit, chit)) {
                            // LogInfo << "DetId " << detit << ", channel " << chit << " triggered" << std::endl;
                            triggeredHits->emplace_back(std::make_pair(detit, chit));
                        }
                    }
                }
                extractedTriggeredHits = true;
            }

            void PrintValidHits(){
                if (!extractedTriggeredHits) ExtractTriggeredHits();
                for (int hitit = 0; hitit < triggeredHits->size(); hitit++) {
                    LogInfo << "DetId " << triggeredHits->at(hitit).first << ", channel " << triggeredHits->at(hitit).second << ", peak: " << GetPeak(triggeredHits->at(hitit).first, triggeredHits->at(hitit).second) << ", baseline: " << GetBaseline(triggeredHits->at(hitit).first, triggeredHits->at(hitit).second) << ", sigma: " << GetSigma(triggeredHits->at(hitit).first, triggeredHits->at(hitit).second) << "\t";
                }
            }

            void PrintInfo() {
                for (int detit = 0; detit < nDetectors; detit++) {
                    for (int chit = 0; chit < nChannels; chit++) {
                        LogInfo << "DetId " << detit << ", channel " << chit << ", peak: " << GetPeak(detit, chit) << ", baseline: " << GetBaseline(detit, chit) << ", sigma: " << GetSigma(detit, chit) << "\t";
            
                    }
                    LogInfo << std::endl;
                }
            }

            void PrintOverview() {
                LogInfo << "Number of detectors: " << nDetectors << std::endl;
                LogInfo << "Number of channels: " << nChannels << std::endl;
                LogInfo << "Number of sigma: " << nsigma << std::endl;
                LogInfo << "Size of peak: " << peak->size() << std::endl;
                LogInfo << "Size of peak for detector 0: " << peak->at(0)->size() << std::endl;
                LogInfo << "Size of peak for detector 1: " << peak->at(1)->size() << std::endl;
                LogInfo << "Size of peak for detector 2: " << peak->at(2)->size() << std::endl;
                LogInfo << "Size of peak for detector 3: " << peak->at(3)->size() << std::endl;
                LogInfo << "Size of baseline: " << baseline.size() << std::endl;
                LogInfo << "Size of sigma: " << sigma.size() << std::endl;
                LogInfo << "Size of triggered hits: " << triggeredHits->size() << std::endl;
            }
    };

    ///////////////////////////
    
    /// Create some objects to plot results

    // Root app
    TApplication *app = new TApplication("app", &argc, argv);

    // Create a vector of TF1 objects to show the channels that fire, one for each detector
    std::vector <TH1F*> *h_firingChannels = new std::vector <TH1F*>;
    h_firingChannels->reserve(nDetectors);
    for (int i = 0; i < nDetectors; i++) {
        TH1F *this_h_firingChannels = new TH1F(Form("Firing channels (Detector %d)", i), Form("Firing channels (Detector %d)", i), nChannels, 0, nChannels);
        this_h_firingChannels->GetXaxis()->SetTitle("Channel");
        this_h_firingChannels->GetYaxis()->SetTitle("Counts");
        h_firingChannels->emplace_back(this_h_firingChannels);
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

    // TODO consider changing branch names?
    raw_events_trees.at(0)->SetBranchAddress("RAW Event", &data->at(0));
    raw_events_trees.at(1)->SetBranchAddress("RAW Event B", &data->at(1));
    raw_events_trees.at(2)->SetBranchAddress("RAW Event C", &data->at(2));
    raw_events_trees.at(3)->SetBranchAddress("RAW Event D", &data->at(3));
    
    for (int entryit = 0; entryit < nEntries.at(0); entryit++) {

        Event this_event; // across detectors

        this_event.SetBaseline(baseline);
        this_event.SetSigma(baseline_sigma); 

        // clear data
        for (int detit = 0; detit < nDetectors; detit++)   data->at(detit)->clear();

        for (int detit = 0; detit < nDetectors; detit++) {
            raw_events_trees.at(detit)->GetEntry(entryit);
            this_event.AddPeak(detit, data->at(detit));
        }
        
        // if (verbose) this_event.PrintInfo(); // this should rather be debug

        
        if (verbose) this_event.PrintOverview();

        this_event.ExtractTriggeredHits();


        std::vector <std::pair<int, int>> *triggeredHits = this_event.GetTriggeredHits();
        if (triggeredHits->size() > 0){
            for (int hitit = 0; hitit < triggeredHits->size(); hitit++) {
                int det = triggeredHits->at(hitit).first;
                int ch = triggeredHits->at(hitit).second;
                h_firingChannels->at(det)->Fill(ch);
            }
        }

        // print values minus baseline for this event
        // if (verbose) this_event.PrintValidHits();      

        // events->emplace_back(this_event);
        
        if (verbose) LogInfo << "Stored entry " << entryit << " in the vector of Events" << std::endl;
    }

    LogInfo << "Stored all entries in the vector of Events" << std::endl;
    // events->at(0).PrintInfo();

    // something wrong with memoeory here

    // // print values minus baseline for each event in events
    // for (int eventit = 0; eventit < events->size(); eventit++) {
    //     LogInfo << "Event " << eventit << std::endl;
    //     events->at(eventit).PrintInfo();
    //     for (int detit = 0; detit < nDetectors; detit++) {
    //         for (int chit = 0; chit < nChannels; chit++) {
    //             LogInfo << "DetId " << detit << ", channel " << chit << ", peak: " << events->at(eventit).GetPeak(detit, chit) << ", baseline: " << events->at(eventit).GetBaseline(detit, chit) << ", sigma: " << events->at(eventit).GetSigma(detit, chit) << "\t";
    //         }
    //         LogInfo << std::endl;
    //     }
    // }
    
    ///////////////////////////

    // plots

    // create a canvas
    LogInfo << "Creating canvas" << std::endl;
    TCanvas *c_channelsFiring = new TCanvas("c_channelsFiring", "c_channelsFiring", 800, 600);
    c_channelsFiring->Divide(2, 2);

    LogInfo << "Drawing histograms" << std::endl;
    for (int i = 0; i < nDetectors; i++) {
        c_channelsFiring->cd(i+1);
        h_firingChannels->at(i)->Draw();
    }
    c_channelsFiring->Update();

    // run the app
    LogInfo << "Running the app" << std::endl;
    app->Run();

    return 0;
}
