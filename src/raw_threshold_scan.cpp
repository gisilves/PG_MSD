#include "TChain.h"
#include "TFile.h"
#include "TH1.h"
#include "TH2.h"
#include "TTree.h"
#include <iostream>
#include <iomanip>

#include "anyoption.h"
#include "event.h"
#include "CLI.hpp"


#define verbose false

int main(int argc, char *argv[])
{

  CLI::App app{"raw_threshold_scan"};

  int NChannels = 384;
  int NVas = 6;
  int minStrip = 0;
  int maxStrip = 383;
  bool absolute = false;
  bool verb = false;
  int commonNoiseType = 0;
  int steps = 0;
  float low_min = 0;
  float low_max = 0;
  float high_min = 0;
  float high_max = 0;
  int version = 0;
  int nevents = -1;

  bool newDAQ = false;
  int side = 0;
  int board = 0;

  std::string calibration, output_filename;
  std::vector<std::string> inputs;

  app.add_flag("-v,--verbose", verb, "Verbose output");
  app.add_option("--version", version, "1212 (6VA miniTRB), 1313 (10VA miniTRB), 2020 (PAPERO)")->required();
  app.add_option("--calibration", calibration, "Calibration file")->required();
  app.add_option("--output", output_filename, "Output ROOT file");
  app.add_option("--minlow", low_min, "Minimum low threshold");
  app.add_option("--maxlow", low_max, "Maximum low threshold");
  app.add_option("--minhigh", high_min, "Minimum high threshold");
  app.add_option("--maxhigh", high_max, "Maximum high threshold");
  app.add_flag("--absolute", absolute, "Use absolute ADC value instead of S/N for thresholds");
  app.add_option("--cn", commonNoiseType, "Common noise type");
  app.add_option("--steps", steps, "Number of steps");  
  app.add_option("--nevents", nevents, "Number of events to process");
  app.add_option("inputs", inputs, "Input ROOT files")->required()->expected(-1);


  CLI11_PARSE(app, argc, argv);

  if (!app.get_option("--version")->count())
  {
    std::cout << "ERROR: no DAQ board version provided" << std::endl;
    return 2;
  }

  if (version == 1212) // original DaMPE miniTRB system
  {
    NChannels = 384;
    NVas = 6;
    minStrip = 0;
    maxStrip = 383;
  }
  else if (version == 1313) // modded DaMPE miniTRB system for the first FOOT prototype
  {
    NChannels = 640;
    NVas = 10;
    minStrip = 0;
    maxStrip = 639;
  }
  else if (version == 2020) // FOOT ADC boards + DE10Nano
  {
    NChannels = 640;
    NVas = 10;
    minStrip = 0;
    maxStrip = 639;
    newDAQ = true;
  }
  else if (version == 2021) // PAN StripX
  {
    NChannels = 2048;
    NVas = 32;
    minStrip = 0;
    maxStrip = 2047;
  }
  else if (version == 2022) // PAN StripY
  {
    NChannels = 128;
    NVas = 1;
    minStrip = 0;
    maxStrip = 127;
  }
  else if (version == 2023) // AMSL0
  {
    NChannels = 1024;
    NVas = 16;
    minStrip = 0;
    maxStrip = 1023;
  }
  else
  {
    std::cout << "ERROR: invalid DAQ board version" << std::endl;
    return 2;
  }

  if (!calibration.empty())
    std::cout << "Calibration file: " << calibration << std::endl;
  else
    std::cout << "No calibration file provided" << std::endl;

  // Create output ROOTfile
  if (output_filename.empty())
  {
    std::cout << "Error: no output file" << std::endl;
    return 2;
  }

  //////////////////Histos
  TH1F *hNclus = new TH1F("hclus", "hclus", 10, -0.5, 9.5);
  hNclus->GetXaxis()->SetTitle("n clusters");

  TH1F *hNstrip = new TH1F("hNstrip", "hNstrip", 10, -0.5, 9.5);
  hNstrip->GetXaxis()->SetTitle("n strips");

  TH2F *hLowVsHigh_nclus = new TH2F("hLowVsHigh_nclus", "hLowVsHigh_nclus", steps, low_min - 0.5, low_max - 0.5, steps, high_min - 0.5, high_max - 0.5);
  hLowVsHigh_nclus->GetXaxis()->SetTitle("Low Threshold");
  hLowVsHigh_nclus->GetYaxis()->SetTitle("High Threshold");

  TH2F *hLowVsHigh_width = new TH2F("hLowVsHigh_width", "hLowVsHigh_width", steps, low_min - 0.5, low_max - 0.5, steps, high_min - 0.5, high_max - 0.5);
  hLowVsHigh_width->GetXaxis()->SetTitle("Low Threshold");
  hLowVsHigh_width->GetYaxis()->SetTitle("High Threshold");

  // Join ROOTfiles in a single chain
  TChain *chain = new TChain("raw_events");
  for (const auto &f : inputs)
  {
    std::cout << "Adding file " << f << " to the chain..." << std::endl;
    chain->Add(f.c_str());
  }

  Long64_t entries = chain->GetEntries();
  printf("This run has %lld entries\n", entries);

  if (nevents > 0)
  {
    entries = nevents;
    printf("Only processing %lld entries\n", entries);
  }

  // Read raw event from input chain TTree
  std::vector<unsigned short> *raw_event = 0;
  TBranch *RAW = 0;
  chain->SetBranchAddress("RAW Event", &raw_event, &RAW);

  // Create output ROOTfile
  TFile *foutput = new TFile(output_filename.c_str(), "RECREATE");
  foutput->cd();

  calib cal;
  read_calib(calibration.c_str(), &cal, NChannels, 2 * board + side, verb);

  float step_low = (float)((low_max - low_min) + 1) / steps;
  float step_high = (float)((high_max - high_min) + 1) / steps;

  int binHigh = 1;

  // cout << "Low threshold: " << low_min << " - " << low_max << endl;
  // cout << "High threshold: " << high_min << " - " << high_max << endl;
  // cout << "Steps: " << step_low << " - " << step_high << endl;

  while (high_min <= high_max)
  {
    int binLow = 1;

    while (low_min <= low_max && low_min <= high_min)
    {
      int perc = 0;
      // Loop over events
      for (int index_event = 0; index_event < entries; index_event++)
      {
        chain->GetEntry(index_event);

        Double_t pperc = 10.0 * ((index_event + 1.0) / entries);
        if (pperc >= perc)
        {
          std::cout << "Processed " << std::setfill('0') << std::setw(7) << (index_event + 1) << " out of " << entries
                    << ": " << std::setfill('0') << std::setw(3) << (int)(100.0 * (index_event + 1.0) / entries) << "%"
                    << " with thresholds "
                    << "L: " << low_min << " H: " << high_min << std::endl;
          perc++;
        }

        std::vector<float> signal;

        if (raw_event->size() == NChannels)
        {
          if (cal.ped.size() >= raw_event->size())
          {
            for (size_t i = 0; i != raw_event->size(); i++)
            {
              if (cal.status[i] == 0)
              {
                signal.push_back(raw_event->at(i) - cal.ped[i]);
              }
              else
              {
                signal.push_back(0);
              }
            }
          }
          else
          {
            if (verbose)
            {
              std::cout << "Error: calibration file is not compatible" << std::endl;
            }
          }
        }

        std::vector<float> signal2(signal.size());

        for (size_t i = 0; i < signal.size(); i++)
        {
          if (GetCN(&signal, i / 64, commonNoiseType))
          {
            signal2.at(i) = signal.at(i) - GetCN(&signal, i / 64, commonNoiseType);
          }
          else
          {
            signal2.at(i) = 0;
          }
        }

        try
        {
          std::vector<cluster> result = clusterize_event(&cal, &signal2, high_min, low_min, 0, 0, absolute, 0, 0, false);

          for (int i = 0; i < result.size(); i++)
          {
            if (i == 0)
            {
              hNclus->Fill(result.size());
            }

            hNstrip->Fill(GetClusterWidth(result.at(i)));
          }
        }
        catch (const char *msg)
        {
          if (verbose)
          {
            std::cerr << msg << "Skipping event " << index_event << std::endl;
          }
          continue;
        }
      }
      float mean_nclus = hNclus->GetMean();
      float mean_width = hNstrip->GetMean();

      hLowVsHigh_nclus->SetBinContent(binLow, binHigh, mean_nclus);
      hLowVsHigh_width->SetBinContent(binLow, binHigh, mean_width);

      hNclus->Reset();
      hNstrip->Reset();

      low_min += step_low;
      binLow++;
    }
    high_min += step_high;
    binHigh++;
  }

  hLowVsHigh_nclus->Write();
  hLowVsHigh_width->Write();

  foutput->Close();
  return 0;
}
