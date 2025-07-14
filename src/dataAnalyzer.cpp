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
#include "TH2F.h"
#include "TCanvas.h"
#include "TGraph.h"
#include "TApplication.h"
#include "TStyle.h"

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
    clp.addTriggerOption("showPlots",       {"--show-plots"},   "Show plots in interactive root session, bool");

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

    // get trees
    // TODO change at converter level, I don't like the names
    std::vector<TTree*> raw_events_trees = std::vector <TTree*>();
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
        TH1F *this_h_amplitude = new TH1F(Form("Amplitude (Detector %d)", i), Form("Amplitude (Detector %d)", i), 100, 0, 1000);
        this_h_amplitude->GetXaxis()->SetTitle("Amplitude");
        this_h_amplitude->GetYaxis()->SetTitle("Counts");
        h_amplitude->emplace_back(this_h_amplitude);
    }

    TH1F *h_hitsInEvent = new TH1F("Hits in event", "Hits in event", 10, -0.5, 10);
    h_hitsInEvent->GetXaxis()->SetTitle("Hits");
    h_hitsInEvent->GetYaxis()->SetTitle("Counts");

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
    
    int limit = nEntries.at(0);
    int setLimit = 50000; // TODO from json settings
    if (limit > setLimit) limit = setLimit;

    int hitsInEvent = 0;
    int triggeredEvents = 0;
    
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
                // LogInfo << "DetId " << detit << ", channel " << chit << ", peak: " << this_event->GetPeak(detit, chit) << ", baseline: " << this_event->GetBaseline(detit, chit) << ", sigma: " << this_event->GetSigma(detit, chit) << "\t";
                g_sigma->at(detit)->SetPoint(g_sigma->at(detit)->GetN(), chit, this_event->GetSigma(detit, chit));
                g_baseline->at(detit)->SetPoint(g_baseline->at(detit)->GetN(), chit, this_event->GetBaseline(detit, chit));
                h_rawPeak->at(detit)->at(chit)->Fill(this_event->GetPeak(detit, chit));

            }
        }

        this_event->ExtractTriggeredHits();

        if (verbose) this_event->PrintOverview();        
        if (debug) this_event->PrintInfo(); // this should rather be debug


        std::vector <std::pair<int, int>> triggeredHits = this_event->GetTriggeredHits();
        if (triggeredHits.size() > 0){
            for (int hitit = 0; hitit < triggeredHits.size(); hitit++) {
                triggeredEvents++;
                hitsInEvent++;
                int det = triggeredHits.at(hitit).first;
                int ch = triggeredHits.at(hitit).second;
                h_firingChannels->at(det)->Fill(ch);
                h_amplitude->at(det)->Fill(this_event->GetPeak(det, ch) - this_event->GetBaseline(det, ch));
            }
        }

        h_hitsInEvent->Fill(hitsInEvent);

        // print values minus baseline for this event
        if (debug) this_event->PrintValidHits();      
        
        if (verbose) LogInfo << "Stored entry " << entryit << " in the vector of Events" << std::endl;

        delete this_event;

    }

    LogInfo << "Read all entries" << std::endl;

    LogInfo << "Number of triggered events: " << (double) triggeredEvents/limit *100 << "%" << std::endl;
    
    ///////////////////////////

    // plots

    // create a canvas
    LogInfo << "Creating canvas" << std::endl;
    std::string runNumber = clp.getOptionVal<std::string>("runNumber");

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

    TCanvas *c_hitsInEvent = new TCanvas(Form("c_hitsInEvent_Run%s", runNumber.c_str()), Form("Hits in Event (Run %s)", runNumber.c_str()), 800, 600);


    LogInfo << "Drawing histograms" << std::endl;
    for (int i = 0; i < nDetectors; i++) {
        c_channelsFiring->cd(i+1);
        h_firingChannels->at(i)->Draw();
    }
    c_channelsFiring->Update();

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

    c_hitsInEvent->cd();
    gPad->SetLogy();
    h_hitsInEvent->Draw();

    // create a pdf report containing firing channels, sigma and amplitude
    std::string outputDir = clp.getOptionVal<std::string>("outputDir");
    LogInfo << "Output directory: " << outputDir << std::endl;
    // output filename same as input root file, but remove .root and add suffixes for each plot type
    std::string input_file_base = input_root_filename.substr(input_root_filename.find_last_of("/\\") + 1);
    size_t lastdot = input_file_base.find_last_of(".");
    if (lastdot != std::string::npos) {
        input_file_base = input_file_base.substr(0, lastdot);
    }
    // Output filenames for each plot type
    std::string output_filename_channelsFiring = outputDir + "/" + input_file_base + "_channelsFiring.pdf";
    std::string output_filename_sigma = outputDir + "/" + input_file_base + "_sigma.pdf";
    std::string output_filename_baseline = outputDir + "/" + input_file_base + "_baseline.pdf";
    std::string output_filename_amplitude = outputDir + "/" + input_file_base + "_amplitude.pdf";
    std::string output_filename_hitsInEvent = outputDir + "/" + input_file_base + "_hitsInEvent.pdf";

    LogInfo << "Output filenames: " << std::endl;
    LogInfo << "Channels Firing: " << output_filename_channelsFiring << std::endl;
    LogInfo << "Sigma: " << output_filename_sigma << std::endl;
    LogInfo << "Baseline: " << output_filename_baseline << std::endl;
    LogInfo << "Amplitude: " << output_filename_amplitude << std::endl;
    LogInfo << "Hits in Event: " << output_filename_hitsInEvent << std::endl;

    // save the canvases to pdf
    c_channelsFiring->SaveAs(output_filename_channelsFiring.c_str());
    c_sigma->SaveAs(output_filename_sigma.c_str());
    c_baseline->SaveAs(output_filename_baseline.c_str());
    c_amplitude->SaveAs(output_filename_amplitude.c_str());
    c_hitsInEvent->SaveAs(output_filename_hitsInEvent.c_str());

    LogInfo << "Printed histograms to " << outputDir << std::endl;
    if (clp.isOptionTriggered("showPlots")){
        LogInfo << "Running the app" << std::endl;
        app->Run();
    }
    else { LogInfo << "If you wish to see the plots, you need to set showPlots option to true.\n";}

    return 0;
}
