//
// Created by Nadrino on 04/07/2025.
//


#include "CmdLineParser.h"
#include "Logger.h"
#include "GenericToolbox.Root.h"

#include <filesystem>
#include <TFile.h>
#include <TTree.h>

#include "GenericToolbox.Root.h"


int main(int argc, char** argv){
  auto parser = CmdLineParser();
  parser.addDummyOption("Required");
  parser.addOption("inputFile", {"-i"}, "Beam monitor root file");
  parser.addOption("calibFile", {"-c"}, "Calibration file");

  parser.addDummyOption("Optional");
  parser.addTriggerOption("calcCovMat", {"--cov"}, "Calculate the cov matrix");

  LogInfo << parser.getDescription().str() << std::endl;

  LogInfo << "Usage: " << std::endl;
  LogInfo << parser.getConfigSummary() << std::endl << std::endl;

  parser.parseCmdLine(argc, argv);

  LogThrowIf( parser.isNoOptionTriggered(), "No option was provided." );

  LogInfo << "Provided arguments: " << std::endl;
  LogInfo << parser.getValueSummary() << std::endl << std::endl;

  std::filesystem::path inputRootFilePath = parser.getOptionVal<std::string>("inputFile");
  std::filesystem::path calibrationFilePath = parser.getOptionVal<std::string>("calibFile");
  bool enableCovarianceCalc{parser.isOptionTriggered("calcCovMat")};

  LogThrowIf(not std::filesystem::exists(inputRootFilePath), "Could not find file " << inputRootFilePath);
  LogThrowIf(not std::filesystem::exists(calibrationFilePath), "Could not find file " << calibrationFilePath);

  LogInfo << "Reading calibration file" << std::endl;
  std::unique_ptr<TFile> calibrationFile = std::make_unique<TFile>(calibrationFilePath.c_str(),"READ");
  LogThrowIf(calibrationFile==nullptr, "Could not open calibration file " << calibrationFilePath);

  auto* calibTree = calibrationFile->Get<TTree>("events");
  LogThrowIf(calibTree==nullptr, "Could not find \"events\" tree within " << calibrationFilePath);

  auto* peakAdcBranch = calibTree->GetBranch("peakAdc");
  LogThrowIf(peakAdcBranch==nullptr, "Could not find \"peakAdc\" branch");

  auto dim = GenericToolbox::getMultiDimArraySizeLeaf(peakAdcBranch);
  LogThrowIf(dim.size() != 2, "Invalid peakAdc dimentions: " << GenericToolbox::toString(dim));

  int nDetectors = int(dim[0]);
  int nChannels = int(dim[1]);

  LogInfo << "nDetectors=" << nDetectors << ", nChannels=" << nChannels << std::endl;

  // declaring buffers
  unsigned int peakAdc[nDetectors][nChannels];
  double averagePeak[nDetectors][nChannels];
  double stdDevPeak[nDetectors][nChannels];

  std::unique_ptr<TMatrixD> covMatrix{nullptr};
  if( enableCovarianceCalc ){
    covMatrix = std::make_unique<TMatrixD>(
      nDetectors*nChannels,
      nDetectors*nChannels
      );
  }

  // only enabling the branch we need for faster read
  calibTree->SetBranchStatus("*", false);
  calibTree->SetBranchStatus("peakAdc", true);
  calibTree->SetBranchAddress("peakAdc", &peakAdc);

  // open output
  std::unique_ptr<TFile> outputFile = std::make_unique<TFile>("test.root","RECREATE");
  GenericToolbox::writeInTFile(
    outputFile.get(),
    TNamed("calibrationFilePath", calibrationFile->GetPath())
    );

  Long64_t nEntries = calibTree->GetEntries();
  // nEntries = std::min(nEntries, int64_t(100));
  for( Long64_t iEntry = 0 ; iEntry < nEntries ; iEntry++ ){
    GenericToolbox::displayProgressBar(iEntry+1, nEntries, "Loading calibration...");
    calibTree->GetEntry(iEntry);

    for( int iDet = 0 ; iDet < nDetectors ; iDet++ ) {
      for( int iCh = 0 ; iCh < nChannels ; iCh++ ) {
        double val_i = static_cast<double>(peakAdc[iDet][iCh]) / static_cast<double>(nEntries);
        averagePeak[iDet][iCh] += val_i;
        stdDevPeak[iDet][iCh] += val_i * val_i * static_cast<double>(nEntries);

        if( covMatrix != nullptr ) {
          int iFlat = iDet*nChannels+iCh;
          for( int jDet = iDet ; jDet < nDetectors ; jDet++ ){
            for( int jCh = iCh ; jCh < nChannels ; jCh++ ) {
              (*covMatrix)[iFlat][jDet*nChannels+jCh]
                += val_i * static_cast<double>(peakAdc[jDet][jCh]);
            }
          }
        }
      }
    }
  }

  LogInfo << "Calculating std-dev..." << std::endl;
  for (int iDet = 0; iDet < nDetectors; ++iDet) {
    for (int iCh = 0; iCh < nChannels; ++iCh) {
      stdDevPeak[iDet][iCh] -= averagePeak[iDet][iCh]*averagePeak[iDet][iCh];

      if( covMatrix != nullptr ) {
        for (int jDet = iDet; jDet < nDetectors; ++jDet) {
          for (int jCh = iCh; jCh < nChannels; ++jCh) {
            (*covMatrix)[iDet*nChannels+iCh][jDet*nChannels+jCh] -=
              averagePeak[iDet][iCh]*averagePeak[jDet][jCh];
            // symmetric
            (*covMatrix)[jDet*nChannels+jCh][iDet*nChannels+iCh]
              = (*covMatrix)[iDet*nChannels+iCh][jDet*nChannels+jCh];
          }
        }
      }

    }
  }

  outputFile->cd();
  auto* outCalibTree = new TTree("calibration", "calibration");

  int detectorIdx;
  int channelIdx;
  double baseline;
  double stddev;
  outCalibTree->Branch("detectorIdx", &detectorIdx);
  outCalibTree->Branch("channelIdx", &channelIdx);
  outCalibTree->Branch("baseline", &baseline);
  outCalibTree->Branch("stddev", &stddev);

  for (detectorIdx = 0; detectorIdx < nDetectors; ++detectorIdx) {
    for (channelIdx = 0; channelIdx < nChannels; ++channelIdx) {
      baseline = averagePeak[detectorIdx][channelIdx];
      stddev = std::sqrt(stdDevPeak[detectorIdx][channelIdx]);
      outCalibTree->Fill();
    }
  }
  outCalibTree->Write(outCalibTree->GetName(), TObject::kOverwrite);

  if( covMatrix != nullptr ) {
    auto* corr = GenericToolbox::convertToCorrelationMatrix(covMatrix.get());
    auto* corrHist = GenericToolbox::convertTMatrixDtoTH2D(
      corr,
      "Calibration covariance matrix",
      "correlation",
      "Channel #", "Channel #");
    corrHist->SetDrawOption("COLZ");
    GenericToolbox::fixTH2display(corrHist);
    corrHist->Write("correlationMatrix");
    delete corrHist;
  }

  std::unique_ptr<TFile> inputFile = std::make_unique<TFile>(inputRootFilePath.c_str(), "READ");
  auto* intputTree = inputFile->Get<TTree>("events");
  unsigned int dataPeakAdc[nDetectors][nChannels];
  intputTree->SetBranchAddress("peakAdc", &dataPeakAdc[0][0]);

  outputFile->cd();
  auto* outputTree = intputTree->CloneTree(0);  // Clone structure, no data
  double peakValue[nDetectors][nChannels];
  outputTree->Branch("peak", &peakValue[0][0], Form("peak[%d][%d]/i", nDetectors, nChannels));

  auto nDataEntries = intputTree->GetEntries();
  for( int64_t iEntry = 0; iEntry < nDataEntries; ++iEntry ) {
    GenericToolbox::displayProgressBar(iEntry, nDataEntries, "Subtracting pedestal...");
    intputTree->GetEntry(iEntry);

    for(int iDet = 0; iDet < nDetectors; ++iDet) {
      for(int iCh = 0; iCh < nChannels; ++iCh) {
        peakValue[iDet][iCh] = double(dataPeakAdc[iDet][iCh]) - averagePeak[iDet][iCh];
      }
    }

    outputTree->Fill();
  }
  outputTree->Write("events", TObject::kOverwrite);

  LogInfo << "Wrote file: " << outputFile->GetPath() << std::endl;
}
