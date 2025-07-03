//
// Created by Nadrino on 03/07/2025.
//

#include "CmdLineParser.h"
#include "Logger.h"

#include "GenericToolbox.Utils.h"
#include "GenericToolbox.Root.h"

#include <vector>
#include <string>

#include "ocaEvent.h"


const int nDetectors{4};
const int nChannels{384};

struct Calibration{
  struct Channel{
    double baselineValue{std::nan("unset")};
    double baselineStdDev{std::nan("unset")};
  };
  Channel channelsList[nDetectors * nChannels];

  [[nodiscard]] const Channel& getChannel(int detectorIdx_, int channelIdx_) const;
  [[nodiscard]] int getFlatIndex(int detectorIdx_, int channelIdx_) const;
  void readFile(const std::string& calibFilePath_);
  void print() const;
  void writeToTree(TDirectory* outDir_) const;

};

struct BeamMonitorEvent{
  double peakValue[nDetectors][nChannels];

  void subtractBaseline(const Calibration& calibration_){
    for( int iDet = 0; iDet < nDetectors; ++iDet) {
      for( int iCh = 0; iCh < nChannels; ++iCh) {
        peakValue[iDet][iCh] -= calibration_.getChannel(iDet, iCh).baselineValue;
      }
    }
  }
  void zeroUnderThreshold(const Calibration& calibration_, double threshold_){
    if( std::isnan(threshold_) ){ return; }
    for( int iDet = 0; iDet < nDetectors; ++iDet) {
      for( int iCh = 0; iCh < nChannels; ++iCh) {
        auto& c = calibration_.getChannel(iDet, iCh);
        if( peakValue[iDet][iCh] < c.baselineValue + threshold_*c.baselineStdDev ){ peakValue[iDet][iCh] = 0; }
      }
    }
  }
  bool allUnderThreshold(const Calibration& calibration_, double threshold_){
    for( int iDet = 0; iDet < nDetectors; ++iDet) {
      for( int iCh = 0; iCh < nChannels; ++iCh) {
        auto& c = calibration_.getChannel(iDet, iCh);
        if( peakValue[iDet][iCh] >= c.baselineValue + threshold_*c.baselineStdDev ) {
          return false;
        }
      }
    }
    return true;
  }
};

int main(int argc, char *argv[]){

  CmdLineParser clp;

  clp.addDummyOption("Mandatory");
  clp.addOption("inputRootFile", {"-i"}, "Primary input root file");
  clp.addOption("outputRootFile", {"-o"}, "Output root file");

  clp.addDummyOption("Optional");
  clp.addOption("calibrationFile", {"-c"}, "Calibration file");
  clp.addOption("threshold", {"-t"}, "threshold X: zero signal if(signal < pedestal + X*sigma) ");
  clp.addTriggerOption("debugVerbose", {"-d"}, "Verbose output");

  LogInfo << clp.getDescription().str() << std::endl;

  LogInfo << "Usage: " << std::endl;
  LogInfo << clp.getConfigSummary() << std::endl << std::endl;

  clp.parseCmdLine(argc, argv);

  LogThrowIf( clp.isNoOptionTriggered(), "No option was provided." );

  LogInfo << "Provided arguments: " << std::endl;
  LogInfo << clp.getValueSummary() << std::endl << std::endl;


  auto inputRootFile = clp.getOptionVal<std::string>("inputRootFile");
  auto outputRootFile = clp.getOptionVal<std::string>("outputRootFile");

  auto calibrationFile = clp.getOptionVal<std::string>("calibrationFile", "");
  auto threshold = clp.isOptionTriggered("threshold") ? clp.getOptionVal<double>("threshold") : std::nan("unset");
  auto debugVerbose = clp.isOptionTriggered("debugVerbose");


  std::unique_ptr<TFile> outputFile(std::make_unique<TFile>(outputRootFile.c_str(), "RECREATE"));

  Calibration calibration;

  if( not calibrationFile.empty() ) {
    calibration.readFile(calibrationFile);
    if(debugVerbose){ calibration.print(); }
    calibration.writeToTree( outputFile.get() );
  }


  std::unique_ptr<TFile> f{GenericToolbox::openExistingTFile(inputRootFile, {"raw_events", "raw_events_B", "raw_events_C", "raw_events_D"})};

  TTree* rawEventTree[nDetectors];
  rawEventTree[0] = f->Get<TTree>("raw_events");
  rawEventTree[1] = f->Get<TTree>("raw_events_B");
  rawEventTree[2] = f->Get<TTree>("raw_events_C");
  rawEventTree[3] = f->Get<TTree>("raw_events_D");

  int nEntries = int(rawEventTree[0]->GetEntries());
  DEBUG_VAR(nEntries);
  for( int iDet = 1; iDet < nDetectors; ++iDet) {
    LogThrowIf(rawEventTree[iDet]->GetEntries() != nEntries,
      "Tree #" << iDet << " has " << rawEventTree[iDet]->GetEntries() << " while #0 has " << nEntries
      );
  }

  std::vector<unsigned int>* dataBuffer[nDetectors];
  for(auto & detectorBuffer : dataBuffer){ detectorBuffer = new std::vector<unsigned int>(nChannels); }
  rawEventTree[0]->SetBranchAddress("RAW Event", &dataBuffer[0]);
  rawEventTree[1]->SetBranchAddress("RAW Event B", &dataBuffer[1]);
  rawEventTree[2]->SetBranchAddress("RAW Event C", &dataBuffer[2]);
  rawEventTree[3]->SetBranchAddress("RAW Event D", &dataBuffer[3]);

  outputFile->cd();
  auto* outputTree = new TTree("events", "events");
  auto* outputZeroSupprTree = new TTree("eventsZeroSuppr", "eventsZeroSuppr");

  unsigned int rawPeakValue[nDetectors][nChannels];
  BeamMonitorEvent bmEventCalibBuffer{};
  BeamMonitorEvent bmEventCalibZeroSupprBuffer{};
  int eventIdx{-1};

  outputTree->Branch("rawPeakValue", &rawPeakValue[0][0], Form("rawPeakValue[%d][%d]/i", nDetectors, nChannels));

  if( not calibrationFile.empty() ) {
    outputTree->Branch("peakValue", &bmEventCalibBuffer.peakValue[0][0], Form("peakValue[%d][%d]/D", nDetectors, nChannels));

    if( not std::isnan(threshold) ) {
      outputZeroSupprTree->Branch("eventIdx", &eventIdx);
      outputZeroSupprTree->Branch("peakValueZeroSuppr", &bmEventCalibZeroSupprBuffer.peakValue[0][0], Form("peakValueZeroSuppr[%d][%d]/D", nDetectors, nChannels));
      outputZeroSupprTree->Branch("peakValue", &bmEventCalibBuffer.peakValue[0][0], Form("peakValue[%d][%d]/D", nDetectors, nChannels));
      outputZeroSupprTree->Branch("rawPeakValue", &rawPeakValue[0][0], Form("rawPeakValue[%d][%d]/i", nDetectors, nChannels));
    }
  }

  LogInfo << "Reading events..." << std::endl;
  for( int iEntry = 0; iEntry < nEntries; ++iEntry ){
    GenericToolbox::displayProgressBar(iEntry, nEntries, "Reading events...");

    for( int iDet = 0; iDet < nDetectors; ++iDet ) {
      rawEventTree[iDet]->GetEntry(iEntry);
      memcpy(&rawPeakValue[iDet][0], dataBuffer[iDet]->data(), nChannels*sizeof(unsigned int));
    }

    if( not calibrationFile.empty() ) {
      // copy + recast to double
      for( int iDet = 0; iDet < nDetectors; ++iDet ) {
        for( int iChannel = 0; iChannel < nChannels; ++iChannel ) {
          bmEventCalibBuffer.peakValue[iDet][iChannel] = static_cast<double>(rawPeakValue[iDet][iChannel]);
        }
      }
      bmEventCalibBuffer.subtractBaseline(calibration);

      if( not std::isnan(threshold) and not bmEventCalibBuffer.allUnderThreshold(calibration, threshold) ){
        eventIdx = iEntry;
        bmEventCalibZeroSupprBuffer = bmEventCalibBuffer;
        bmEventCalibZeroSupprBuffer.zeroUnderThreshold(calibration, threshold);

        outputZeroSupprTree->Fill();
      }
    }

    outputTree->Fill();
  }
  outputTree->Write(outputTree->GetName(), TObject::kOverwrite);
  outputZeroSupprTree->Write(outputZeroSupprTree->GetName(), TObject::kOverwrite);

  LogInfo << "Output writen as " << outputFile->GetPath() << std::endl;
}


const Calibration::Channel& Calibration::getChannel(int detectorIdx_, int channelIdx_) const {
  return channelsList[getFlatIndex(detectorIdx_, channelIdx_)];
}
void Calibration::readFile(const std::string& calibFilePath_){
  LogInfo << "Reading calibration file: " << calibFilePath_ << std::endl;

  std::ifstream calFile(calibFilePath_);
  if (not calFile.is_open()) {
    LogError << "Error: cannot open calibration file: " << calibFilePath_ << std::endl;
    LogExit("invalid cal file");
  }

  std::string line;
  for (int det = 0; det < nDetectors; ++det) {
    // Skip 18 header lines per detector block
    for (int i = 0; i < 18; ++i) std::getline(calFile, line);

    for (int ch = 0; ch < nChannels; ++ch) {
      if (!std::getline(calFile, line)) {
        LogError << "Error: unexpected end of file" << std::endl;
        LogExit("invalid cal file");
      }

      std::istringstream iss(line);
      std::vector<float> values;
      std::string token;
      while (std::getline(iss, token, ',')) {
        float val;
        std::istringstream(token) >> val;
        values.emplace_back(val);
      }

      if (values.size() != 8) {
        std::cerr << "Error: wrong number of values in calibration file (got " << values.size() << ")\n";
        return;
      }

      auto &channel = channelsList[getFlatIndex(
        det,
        static_cast<int>(values[0])
        )];
      channel.baselineValue = values[3];
      channel.baselineStdDev = values[5];
    }
  }

  LogInfo << "Calibration loaded successfully." << std::endl;
}
int Calibration::getFlatIndex(int detectorIdx_, int channelIdx_) const {
  int flatIndex = detectorIdx_ * nChannels + channelIdx_;
  LogThrowIf(flatIndex >= nDetectors * nChannels or flatIndex <= -1);
  return flatIndex;
}
void Calibration::print() const {
  LogDebug << "Printing calibration..." << std::endl;
  GenericToolbox::TablePrinter t;
  t << "Detector" << GenericToolbox::TablePrinter::NextColumn;
  t << "Channel" << GenericToolbox::TablePrinter::NextColumn;
  t << "Baseline" << GenericToolbox::TablePrinter::NextColumn;
  t << "Std-dev" << GenericToolbox::TablePrinter::NextLine;

  for( int iDet = 0; iDet < nDetectors; ++iDet){
    for( int iChannel = 0; iChannel < nChannels; ++iChannel) {
      auto& c = getChannel(iDet, iChannel);
      LogScopeIndent;
      t << iDet << GenericToolbox::TablePrinter::NextColumn;
      t << iChannel << GenericToolbox::TablePrinter::NextColumn;
      t << c.baselineValue << GenericToolbox::TablePrinter::NextColumn;
      t << c.baselineStdDev << GenericToolbox::TablePrinter::NextColumn;
    }
  }

  t.printTable();
}
void Calibration::writeToTree(TDirectory* outDir_) const {
  LogThrowIf(outDir_==nullptr);
  LogInfo << "Writing calibration to " << outDir_->GetName() << std::endl;
  outDir_->cd();
  auto* calibTree = new TTree("calibration", "calibration");

  struct CalibEntry{
    int detectorIdx{-1};
    int channelIdx{-1};
    double baselineMeanValue{std::nan("unset")};
    double baselineStdDev{std::nan("unset")};
  };
  CalibEntry buffer;

  calibTree->Branch("detectorIdx", &buffer.detectorIdx);
  calibTree->Branch("channelIdx", &buffer.channelIdx);
  calibTree->Branch("baselineMeanValue", &buffer.baselineMeanValue);
  calibTree->Branch("baselineStdDev", &buffer.baselineStdDev);

  for( int iDet = 0; iDet < nDetectors; ++iDet ){
    for( int iChannel = 0; iChannel < nChannels; ++iChannel) {
      buffer.detectorIdx = iDet;
      buffer.channelIdx = iChannel;
      auto& c = getChannel(iDet, iChannel);
      buffer.baselineMeanValue = c.baselineValue;
      buffer.baselineStdDev = c.baselineStdDev;
      calibTree->Fill();
    }
  }

  calibTree->Write();
}
