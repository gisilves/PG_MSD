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


int main(int argc, char **argv){
  LogInfo << "Running raw to ROOT converter..." << std::endl;

  CmdLineParser parser = CmdLineParser();

  parser.addDummyOption("Required");
  parser.addOption("inputFile", {"-i"}, "Input .dat file to be converted.");

  parser.addDummyOption("Optional");
  parser.addOption("outputFolder", {"-o"}, "Output folder where the ROOT file will be writen.");
  parser.addTriggerOption("verbose", {"-v"}, "Enable verbode");

  LogInfo << parser.getDescription().str() << std::endl;
  LogInfo << "Usage: " << std::endl;
  LogInfo << parser.getConfigSummary() << std::endl << std::endl;

  parser.parseCmdLine(argc, argv);

  LogThrowIf( parser.isNoOptionTriggered(), "No option was provided." );

  LogInfo << "Provided arguments: " << std::endl;
  LogInfo << parser.getValueSummary() << std::endl << std::endl;

  auto inputCalFilePath = parser.getOptionVal<std::string>("inputFile");

  bool verbose = parser.isOptionTriggered("verbose");
  std::string outFolderPath{};
  if( parser.isOptionTriggered("outputFolder") ){ outFolderPath = parser.getOptionVal<std::string>("outputFolder"); }

  std::filesystem::path outputRootFilePath(inputCalFilePath);
  outputRootFilePath.replace_extension(".root");
  outputRootFilePath = outFolderPath / outputRootFilePath.filename();
  LogWarning << inputCalFilePath << " -> " << outputRootFilePath << std::endl;

  auto inputCalFile = std::fstream(inputCalFilePath, std::ios::in | std::ios::out | std::ios::binary);
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

  tree->Branch("size", &bmEvent.eventSize);
  tree->Branch("fwVersion", &bmEvent.fwVersion);
  tree->Branch("triggerNumber", &bmEvent.triggerNumber);
  tree->Branch("boardId", &bmEvent.boardId);
  tree->Branch("timestamp", &bmEvent.timestamp);
  tree->Branch("extTimestamp", &bmEvent.extTimestamp);
  tree->Branch("triggerId", &bmEvent.triggerId);
  tree->Branch("peak", &bmEvent.peakValue, Form("peak[%d][%d]/i", N_DETECTORS, N_CHANNELS));

  LogInfo << "Reading " << nEntries << " entries..." << std::endl;
  inputCalFile = std::fstream(inputCalFilePath, std::ios::in | std::ios::out | std::ios::binary);
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
    read_event(inputCalFile, offset, int(bmEvent.eventSize), verbose, false);



    tree->Fill();

    // next offset
    offset = (uint64_t) inputCalFile.tellg() + padding_offset + 8;

  }
  tree->Write(tree->GetName(), TObject::kOverwrite);

  LogInfo << "Written " << outputRootFile->GetPath() << std::endl;
  return EXIT_SUCCESS;
}
