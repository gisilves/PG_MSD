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

#include "GenericToolbox.Root.h"
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
  parser.addOption("threshold", {"-t"}, "Skip event with all channel peaks < baseline + threshold*std-dev (requires calib file)");
  parser.addOption("maxNbEvents", {"-me"}, "Stop after reading N events");
  parser.addTriggerOption("writeCalibData", {"--is-calib"}, "Will compute the baseline, the std-dev and create the calib TTree");
  parser.addTriggerOption("calcCovCalib", {"--cov"}, "Calculate covariance matrix of the std-dev");
  parser.addTriggerOption("skipEventTree", {"--skip-event"}, "Don't write the events TTree (useful if calib)");
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
  int nMaxEvents{-1};
  if(parser.isOptionTriggered("maxNbEvents") ){ nMaxEvents = parser.getOptionVal<int>("maxNbEvents"); }
  bool verbose = parser.isOptionTriggered("verbose");
  bool skipEventTree = parser.isOptionTriggered("skipEventTree");
  bool writeCalibData = parser.isOptionTriggered("writeCalibData");
  bool zeroSuppress = parser.isOptionTriggered("threshold");
  bool calcCovCalib = parser.isOptionTriggered("calcCovCalib");
  double threshold{std::nan("unset")};
  if(zeroSuppress) threshold = parser.getOptionVal<double>("threshold");

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
  double peakBaseline[N_DETECTORS][N_CHANNELS]{};
  double peakStdDev[N_DETECTORS][N_CHANNELS]{};

  std::unique_ptr<TFile> calibFile{nullptr};
  TTree *calibTree{nullptr};
  if( not calibFilePath.empty() ){
    LogInfo << "Reading calibration data from " << calibFilePath << std::endl;
    int detectorIdx; int channelIdx; double baseline; double stddev;
    calibFile = std::make_unique<TFile>(calibFilePath.c_str(), "READ");
    LogThrowIf(calibFile==nullptr, "Can't open calibration file.");
    calibTree = calibFile->Get<TTree>("calibration");
    LogThrowIf(calibTree==nullptr, "Can't open calibration tree.");

    calibTree->SetBranchAddress("detectorIdx", &detectorIdx);
    calibTree->SetBranchAddress("channelIdx", &channelIdx);
    calibTree->SetBranchAddress("baseline", &baseline);
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

    if( nMaxEvents != -1 and nEntries == nMaxEvents ){ break; }

    // check for event header if this is the first board
    if( not read_evt_header(inputCalFile, offset, verbose) ){ break; }

    bmEvent.readTuple( read_de10_header(inputCalFile, offset, verbose) );

    if( not bmEvent.isGood ){ continue; }

    offset = bmEvent.offset;
    read_event(inputCalFile, offset, int(bmEvent.eventSize), verbose, false);
    offset = (uint64_t) inputCalFile.tellg() + padding_offset + 8;

  }

  std::unique_ptr<TFile> outputRootFile = std::make_unique<TFile>(outputRootFilePath.c_str(), "RECREATE");

  if( not calibFilePath.empty() ) {
    outputRootFile->cd();
    GenericToolbox::writeInTFile(outputRootFile.get(), TNamed("calibFilePath", calibFilePath.c_str()));
    calibTree->CloneTree()->Write();
  }

  outputRootFile->cd();
  TTree* tree{nullptr};

  if( not skipEventTree ) {
    tree = new TTree("events", "events");

    // tree->Branch("size", &bmEvent.eventSize);
    // tree->Branch("fwVersion", &bmEvent.fwVersion);
    tree->Branch("triggerNumber", &bmEvent.triggerNumber);
    // tree->Branch("boardId", &bmEvent.boardId); // always board 0
    tree->Branch("timestamp", &bmEvent.timestamp);
    tree->Branch("extTimestamp", &bmEvent.extTimestamp);
    tree->Branch("triggerId", &bmEvent.triggerId);
    tree->Branch("peakAdc", &bmEvent.peakAdc, Form("peakAdc[%d][%d]/i", N_DETECTORS, N_CHANNELS));
    // tree->Branch("peakAdcSum", &bmEvent.peakAdcSum, Form("peakAdcSum[%d]/i", N_DETECTORS));

    if( not calibFilePath.empty() ){
      tree->Branch("peak", &bmEvent.peak, Form("peak[%d][%d]/D", N_DETECTORS, N_CHANNELS));
      tree->Branch("peakSum", &bmEvent.peakSum, Form("peakSum[%d]/D", N_DETECTORS));
      if( zeroSuppress ) {
        tree->Branch("peakZeroSuppr", &bmEvent.peakZeroSuppr, Form("peakZeroSuppr[%d][%d]/D", N_DETECTORS, N_CHANNELS));
        tree->Branch("peakZeroSupprSum", &bmEvent.peakZeroSupprSum, Form("peakZeroSupprSum[%d]/D", N_DETECTORS));
        tree->Branch("xBarycenter", &bmEvent.xBarycenter, Form("xBarycenter[%d]/D", N_DETECTORS));
        tree->Branch("yBarycenter", &bmEvent.yBarycenter, Form("yBarycenter/D"));
      }
    }
  }

  std::unique_ptr<TMatrixD> covMatrix{nullptr};
  if( calcCovCalib ){
    covMatrix = std::make_unique<TMatrixD>(
      N_DETECTORS*N_CHANNELS,
      N_DETECTORS*N_CHANNELS
      );
    for(int iFlat = 0; iFlat < N_DETECTORS*N_CHANNELS; ++iFlat ) {
      for(int jFlat = 0; jFlat < N_DETECTORS*N_CHANNELS; ++jFlat ) {
        (*covMatrix)[iFlat][jFlat] = 0;
      }
    }
  }

  LogInfo << "Reading " << nEntries << " entries..." << std::endl;
  inputCalFile = std::fstream(inputDatFilePath, std::ios::in | std::ios::out | std::ios::binary);
  offset = seek_first_evt_header(inputCalFile, 0, verbose);
  auto iEvent{nEntries}; iEvent = 0;
  auto nWriten{iEvent};
  while( not inputCalFile.eof() ) {
    iEvent++;
    GenericToolbox::displayProgressBar(iEvent, nEntries, "Writing events...");

    if( iEvent == nEntries ){ break; }

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

    bool skip{true}; // if zeroSuppress and at no signal is over the threshold
    bmEvent.yBarycenter=0;
    for (size_t iDet = 0; iDet < N_DETECTORS; ++iDet) {
      memcpy(&bmEvent.peakAdc[iDet][0], &data[iDet * N_CHANNELS], N_CHANNELS * sizeof(unsigned int));
      bmEvent.peakAdcSum[iDet] = std::accumulate(
              &bmEvent.peakAdc[iDet][0],
              &bmEvent.peakAdc[iDet][N_CHANNELS],
              static_cast<uint32_t>(0)
            );

      if( not calibFilePath.empty() ) {
        bmEvent.xBarycenter[iDet] = 0;
        for( size_t iCh = 0; iCh < N_CHANNELS; ++iCh ) {
          bmEvent.peak[iDet][iCh] = static_cast<double>(bmEvent.peakAdc[iDet][iCh]) - peakBaseline[iDet][iCh];

          if( zeroSuppress and bmEvent.peak[iDet][iCh] >= peakStdDev[iDet][iCh]*threshold ) {
            bmEvent.peakZeroSuppr[iDet][iCh] = bmEvent.peak[iDet][iCh];
            bmEvent.xBarycenter[iDet] += double(iCh)*bmEvent.peakZeroSuppr[iDet][iCh];
            skip = false;
          }
        }

        bmEvent.peakSum[iDet] = std::accumulate(
              &bmEvent.peak[iDet][0],
              &bmEvent.peak[iDet][N_CHANNELS],
              0.0
            );
        if( zeroSuppress ) {
          bmEvent.peakZeroSupprSum[iDet] = std::accumulate(
              &bmEvent.peakZeroSuppr[iDet][0],
              &bmEvent.peakZeroSuppr[iDet][N_CHANNELS],
              0.0
            );

          bmEvent.xBarycenter[iDet] /= bmEvent.peakZeroSupprSum[iDet];
          bmEvent.xBarycenter[iDet] -= double(N_CHANNELS)/2.;

          bmEvent.yBarycenter += std::sin(double(iDet)*15.*M_PI/180.)*bmEvent.xBarycenter[iDet]/2.; // 2 detectors will give this info
        }
      }
    }
    if( zeroSuppress and skip ){ continue; } // skip TTree::Fill();

    if( not skipEventTree ){ tree->Fill(); nWriten++; }

    if( writeCalibData ){
      for( int iDet = 0 ; iDet < N_DETECTORS ; iDet++ ) {
        for( int iCh = 0 ; iCh < N_CHANNELS ; iCh++ ) {
          auto val_i = static_cast<double>(bmEvent.peakAdc[iDet][iCh]);
          peakBaseline[iDet][iCh] += val_i;
          peakStdDev[iDet][iCh] += val_i * val_i;

          if( covMatrix != nullptr ) {
            int iFlat = iDet*N_CHANNELS+iCh;
            for (int jFlat = iFlat; jFlat < N_DETECTORS * N_CHANNELS; ++jFlat) {
              auto val_j = static_cast<double>(bmEvent.peakAdc[jFlat / N_CHANNELS][jFlat % N_CHANNELS]);
              (*covMatrix)[iFlat][jFlat] += val_i * val_j;
            }
          }
        }
      }
    }

    // next offset
    offset = static_cast<uint64_t>(inputCalFile.tellg()) + padding_offset + 8;

  }
  if( not skipEventTree ) {
    LogInfo << nWriten << " events have been writen." << std::endl;
    tree->Write(tree->GetName(), TObject::kOverwrite);
  }

  if( writeCalibData ){
    LogInfo << "Writing calibration data..." << std::endl;
    for (int iDet = 0; iDet < N_DETECTORS; ++iDet) {
      for (int iCh = 0; iCh < N_CHANNELS; ++iCh) {
        peakBaseline[iDet][iCh] /= double(nEntries);
        peakStdDev[iDet][iCh] = std::sqrt(
          peakStdDev[iDet][iCh] / double(nEntries) - peakBaseline[iDet][iCh] * peakBaseline[iDet][iCh]
        );
      }
    }

    if( covMatrix != nullptr ) {
      LogInfo << "Writing correlation matrix..." << std::endl;
      for (int i = 0; i < N_DETECTORS * N_CHANNELS; ++i) {
        for (int j = i; j < N_DETECTORS * N_CHANNELS; ++j) {
          double mean_i = peakBaseline[i / N_CHANNELS][i % N_CHANNELS];
          double mean_j = peakBaseline[j / N_CHANNELS][j % N_CHANNELS];
          (*covMatrix)[i][j] = ((*covMatrix)[i][j] / double(nEntries)) - mean_i * mean_j;
          (*covMatrix)[j][i] = (*covMatrix)[i][j];
        }
      }

      outputRootFile->cd();
      auto* corr = GenericToolbox::convertToCorrelationMatrix(covMatrix.get());
      auto* corrHist = GenericToolbox::convertTMatrixDtoTH2D(
        corr,
        "Calibration covariance matrix",
        "correlation",
        "Channel #", "Channel #");

      corrHist->SetMinimum(-1);
      corrHist->SetMaximum(1);
      corrHist->SetDrawOption("COLZ");
      GenericToolbox::fixTH2display(corrHist);
      corrHist->Write("correlationMatrix");
      delete corrHist;
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
