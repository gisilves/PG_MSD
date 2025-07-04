//
// Created by Nadrino on 04/07/2025.
//

#include "bmRawToRootConverter.h"
#include "PAPERO.h"

#include "CmdLineParser.h"
#include "Logger.h"

#include "TFile.h"
#include "TTree.h"

#include <filesystem>
#include <cstdlib>

#include "GenericToolbox.Utils.h"


template <typename T>
std::vector<T> reorder_DUNE(std::vector<T> const &v)
{
  std::vector<T> reordered_vec(v.size());
  int j = 0;
  constexpr int order[] = {1, 0, 3, 2, 4, 8, 6, 5, 9, 7};
  for (int ch = 0; ch < 192; ch++)
  {
    for (int adc : order)
    {
      reordered_vec.at(adc * 192 + ch) = v.at(j);
      j++;
    }
  }
  return reordered_vec;
}



int main(int argc, char **argv){
  LogInfo << "Running raw to ROOT converter..." << std::endl;

  CmdLineParser parser = CmdLineParser();

  parser.addDummyOption("Required");
  parser.addOption("inputFile", {"-i"}, "Input .dat file to be converted.");

  parser.addDummyOption("Optional");
  parser.addOption("outputFolder", {"-o"}, "Output folder where the ROOT file will be writen.");
  parser.addOption("calibrationFile", {"-c"}, "Use calibration ROOT file (calib data processed by this app).");
  parser.addTriggerOption("writeCalibData", {"--is-calib"}, "Will compute the baseline, the std-dev and create the calib TTree");
  parser.addTriggerOption("verbose", {"-v"}, "Enable verbode");

  LogInfo << parser.getDescription().str() << std::endl;
  LogInfo << "Usage: " << std::endl;
  LogInfo << parser.getConfigSummary() << std::endl << std::endl;

  parser.parseCmdLine(argc, argv);

  LogThrowIf( parser.isNoOptionTriggered(), "No option was provided." );

  LogInfo << "Provided arguments: " << std::endl;
  LogInfo << parser.getValueSummary() << std::endl << std::endl;

  auto inputDatFilePath = parser.getOptionVal<std::string>("inputFile");
  std::string calibFilePath = {};
  if(parser.isOptionTriggered("calibrationFile")) {
    calibFilePath = parser.getOptionVal<std::string>("calibrationFile");
  }
  bool verbose = parser.isOptionTriggered("verbose");
  bool writeCalibData = parser.isOptionTriggered("writeCalibData");

  LogExitIf(
    writeCalibData and not calibFilePath.empty(),
    "Can't write calib data if using external calib file."
    );

  std::string outFolderPath{};
  if( parser.isOptionTriggered("outputFolder") ){ outFolderPath = parser.getOptionVal<std::string>("outputFolder"); }

  std::filesystem::path outputRootFilePath(inputDatFilePath);
  outputRootFilePath.replace_extension(".root");
  outputRootFilePath = outFolderPath / outputRootFilePath.filename();
  LogWarning << inputDatFilePath << " -> " << outputRootFilePath << std::endl;

  // calib (write or read)
  double peakBaseline[N_DETECTORS][N_CHANNELS];
  double peakStdDev[N_DETECTORS][N_CHANNELS];

  if( not calibFilePath.empty() ){
    int detectorIdx; int channelIdx; double baseline; double stddev;
    std::unique_ptr<TFile> calibFile = std::make_unique<TFile>(inputDatFilePath.c_str(), "READ");
    LogThrowIf(calibFile==nullptr, "Can't open calibration file.");
    auto *calibTree = calibFile->Get<TTree>("events");
    LogThrowIf(calibTree==nullptr, "Can't open calibration tree.");

    calibTree->SetBranchAddress("detectorIdx", &detectorIdx);
    calibTree->SetBranchAddress("channelIdx", &channelIdx);
    calibTree->SetBranchAddress("baseline", &peakBaseline);
    calibTree->SetBranchAddress("stddev", &stddev);

    int nCalibEntries = int(calibTree->GetEntries());
    LogThrowIf(nCalibEntries != N_DETECTORS*N_CHANNELS, "Wrong number of channels in calibration file.");
    for( int iEntry = 0; iEntry < nCalibEntries; ++iEntry ){
      calibTree->GetEntry(iEntry);
      peakBaseline[detectorIdx][channelIdx] = baseline;
      peakStdDev[detectorIdx][channelIdx] = stddev;
    }
  }

  auto inputCalFile = std::fstream(inputDatFilePath, std::ios::in | std::ios::out | std::ios::binary);
  LogThrowIf(inputCalFile.fail());

  BeamMonitorEventBuffer bmEvent{};

  uint64_t offset = 0;
  uint64_t padding_offset = 0;
  uint64_t nEntries{0};

  LogInfo << "Checking the nb of entries..." << std::endl;
  offset = seek_first_evt_header(inputCalFile, 0, verbose);
  while( not inputCalFile.eof() ) {
    nEntries++;

    // check for event header if this is the first board
    if( not read_evt_header(inputCalFile, offset, verbose) ){ break; }

    bmEvent.readTuple( read_de10_header(inputCalFile, offset, verbose) );

    if( not bmEvent.isGood ){ continue; }

    offset = bmEvent.offset;
    read_event(inputCalFile, offset, int(bmEvent.eventSize), verbose, false);
    offset = (uint64_t) inputCalFile.tellg() + padding_offset + 8;

  }

  std::unique_ptr<TFile> outputRootFile = std::make_unique<TFile>(outputRootFilePath.c_str(), "RECREATE");

  outputRootFile->cd();
  auto* tree = new TTree("events", "events");

  // tree->Branch("size", &bmEvent.eventSize);
  // tree->Branch("fwVersion", &bmEvent.fwVersion);
  tree->Branch("triggerNumber", &bmEvent.triggerNumber);
  // tree->Branch("boardId", &bmEvent.boardId); // always board 0
  tree->Branch("timestamp", &bmEvent.timestamp);
  tree->Branch("extTimestamp", &bmEvent.extTimestamp);
  tree->Branch("triggerId", &bmEvent.triggerId);
  tree->Branch("peakAdc", &bmEvent.peakAdc, Form("peakAdc[%d][%d]/i", N_DETECTORS, N_CHANNELS));

  if( not calibFilePath.empty() ){
    tree->Branch("peak", &bmEvent.peak, Form("peak[%d][%d]/i", N_DETECTORS, N_CHANNELS));
  }

  LogInfo << "Reading " << nEntries << " entries..." << std::endl;
  inputCalFile = std::fstream(inputDatFilePath, std::ios::in | std::ios::out | std::ios::binary);
  offset = seek_first_evt_header(inputCalFile, 0, verbose);
  auto iEvent{nEntries}; iEvent = 0;
  while( not inputCalFile.eof() ) {
    iEvent++;
    GenericToolbox::displayProgressBar(iEvent, nEntries, "Writing events...");

    // check for event header if this is the first board
    if( not read_evt_header(inputCalFile, offset, verbose) ){ break; }

    bmEvent.readTuple( read_de10_header(inputCalFile, offset, verbose) );

    if( not bmEvent.isGood ){ continue; }

    offset = bmEvent.offset;
    auto data = read_event(inputCalFile, offset, int(bmEvent.eventSize), verbose, false);
    data = reorder_DUNE(data);

    // should be the amount of channel
    size_t nbOfValuesPerDet = 2 * data.size() / ADC_N;
    LogThrowIf(nbOfValuesPerDet - N_CHANNELS != 0, "Invalid data size: " << nbOfValuesPerDet - N_CHANNELS);

    for (size_t det = 0; det < N_DETECTORS; ++det) {
      memcpy(&bmEvent.peakAdc[det][0], &data[det * N_CHANNELS], N_CHANNELS * sizeof(unsigned int));

      if( not calibFilePath.empty() ) {
        for( size_t iCh = 0; iCh < N_CHANNELS; ++iCh ) {
          bmEvent.peak[det][iCh] = static_cast<double>(bmEvent.peakAdc[det][iCh])/peakBaseline[det][iCh];
        }
      }
    }

    if( writeCalibData ){
      for( int iDet = 0 ; iDet < N_DETECTORS ; iDet++ ) {
        for( int iCh = 0 ; iCh < N_CHANNELS ; iCh++ ) {
          double val_i = static_cast<double>(bmEvent.peakAdc[iDet][iCh]) / static_cast<double>(nEntries);
          peakBaseline[iDet][iCh] += val_i;
          peakStdDev[iDet][iCh] += val_i * val_i * static_cast<double>(nEntries);
        }
      }
    }

    tree->Fill();

    // next offset
    offset = static_cast<uint64_t>(inputCalFile.tellg()) + padding_offset + 8;

  }
  tree->Write(tree->GetName(), TObject::kOverwrite);

  if( writeCalibData ){
    for( int iDet = 0 ; iDet < N_DETECTORS ; iDet++ ) {
      for( int iCh = 0 ; iCh < N_CHANNELS ; iCh++ ) {
        peakStdDev[iDet][iCh] -= std::pow(peakBaseline[iDet][iCh], 2);
        peakStdDev[iDet][iCh] = std::sqrt(peakStdDev[iDet][iCh]);
      }
    }

    outputRootFile->cd();
    auto* outCalibTree = new TTree("calibration", "calibration");
    int detectorIdx;
    int channelIdx;
    double baseline;
    double stddev;
    outCalibTree->Branch("detectorIdx", &detectorIdx);
    outCalibTree->Branch("channelIdx", &channelIdx);
    outCalibTree->Branch("baseline", &baseline);
    outCalibTree->Branch("stddev", &stddev);

    for (detectorIdx = 0; detectorIdx < N_DETECTORS; ++detectorIdx) {
      for (channelIdx = 0; channelIdx < N_CHANNELS; ++channelIdx) {
        baseline = peakBaseline[detectorIdx][channelIdx];
        stddev = std::sqrt(peakStdDev[detectorIdx][channelIdx]);
        outCalibTree->Fill();
      }
    }
    outCalibTree->Write(outCalibTree->GetName(), TObject::kOverwrite);
  }

  LogInfo << "Written " << outputRootFile->GetPath() << std::endl;
  return EXIT_SUCCESS;
}
