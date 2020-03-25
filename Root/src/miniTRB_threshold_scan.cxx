#include "TChain.h"
#include "TFile.h"
#include "TH1.h"
#include "TH2.h"
#include "TTree.h"
#include "omp.h"
#include <iostream>
#include <iomanip>

#include "miniTRB.h"

#define verbose false

int main(int argc, char *argv[])
{
  if (argc < 11)
  {
    std::cout
        << "Usage:\n ./miniTRB_threshold_scan <output_rootfile> <calibration file> "
           "<min low> <max low> <min high> <max high> <use absolute thresholds> <common noise type> "
           "<steps> <first input root-filename> [second input root-filename] ..."
        << std::endl;
    return 1;
  }

  int steps = atoi(argv[9]);

  //////////////////Histos
  TH1F *hNclus = new TH1F("hclus", "hclus", 10, -0.5, 9.5);
  hNclus->GetXaxis()->SetTitle("n clusters");

  TH1F *hNstrip = new TH1F("hNstrip", "hNstrip", 10, -0.5, 9.5);
  hNstrip->GetXaxis()->SetTitle("n strips");

  TH2F *hLowVsHigh_nclus = new TH2F("hLowVsHigh_nclus", "hLowVsHigh_nclus", steps, atoi(argv[3]) - 0.5, atoi(argv[4]) - 0.5, steps, atoi(argv[5]) - 0.5, atoi(argv[6]) - 0.5);
  hLowVsHigh_nclus->GetXaxis()->SetTitle("Low Threshold");
  hLowVsHigh_nclus->GetYaxis()->SetTitle("High Threshold");

  TH2F *hLowVsHigh_width = new TH2F("hLowVsHigh_width", "hLowVsHigh_width", steps, atoi(argv[3]) - 0.5, atoi(argv[4]) - 0.5, steps, atoi(argv[5]) - 0.5, atoi(argv[6]) - 0.5);
  hLowVsHigh_width->GetXaxis()->SetTitle("Low Threshold");
  hLowVsHigh_width->GetYaxis()->SetTitle("High Threshold");

  // Join ROOTfiles in a single chain
  TChain *chain = new TChain("raw_events");
  for (int ii = 10; ii < argc; ii++)
  {
    std::cout << "Adding file " << argv[ii] << " to the chain..." << std::endl;
    chain->Add(argv[ii]);
  }

  Long64_t entries = chain->GetEntries();
  // int entries = 2;
  printf("This run has %lld entries\n", entries);

  // Read raw event from input chain TTree
  std::vector<unsigned short> *raw_event = 0;
  TBranch *RAW = 0;
  chain->SetBranchAddress("RAW Event", &raw_event, &RAW);

  // Create output ROOTfile
  TString output_filename = argv[1];
  TFile *foutput = new TFile(output_filename.Data(), "RECREATE");
  foutput->cd();

  calib cal;
  read_calib(argv[2], &cal);

  float low_min = atoi(argv[3]);
  float low_max = atoi(argv[4]);
  float high_min = atoi(argv[5]);
  float high_max = atoi(argv[6]);

  float step_low = ((low_max - low_min) + 1) / steps;
  float step_high = ((high_max - high_min) + 1) / steps;

  int binHigh = 1;

  while (high_min <= high_max)
  {
    low_min = atoi(argv[3]);
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

        if (raw_event->size() == 384 || raw_event->size() == 640)
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

#pragma omp parallel for
        for (size_t i = 0; i < signal.size(); i++)
        {
          if (GetCN(&signal, i / 64, atoi(argv[9])))
          {
            signal2.at(i) = signal.at(i) - GetCN(&signal, i / 64, atoi(argv[8]));
          }
          else
          {
            signal2.at(i) = 0;
          }
        }

        try
        {
          std::vector<cluster> result = clusterize(&cal, &signal2, high_min, low_min, 0, 0, atoi(argv[7]));

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
