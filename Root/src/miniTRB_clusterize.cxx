#include "TChain.h"
#include "TFile.h"
#include "TH1.h"
#include "TH2.h"
#include "TTree.h"
#include "omp.h"
#include <iostream>

#include "miniTRB.h"

#define verbose false
#define NChannels 384
#define NVas 6
#define minStrip 0
#define maxStrip 383

int main(int argc, char *argv[])
{

  //////////////////Histos
  TH1F *hEnergyCluster =
      new TH1F("hEnergyCluster", "hEnergyCluster", 2000, 0, 1000);
  hEnergyCluster->GetXaxis()->SetTitle("ADC");

  TH1F *hEnergyCluster1Strip =
      new TH1F("hEnergyCluster1Strip", "hEnergyCluster1Strip", 2000, 0, 1000);
  hEnergyCluster1Strip->GetXaxis()->SetTitle("ADC");

  TH1F *hEnergyCluster2Strip =
      new TH1F("hEnergyCluster2Strip", "hEnergyCluster2Strip", 2000, 0, 1000);
  hEnergyCluster2Strip->GetXaxis()->SetTitle("ADC");

  TH1F *hEnergyClusterManyStrip = new TH1F(
      "hEnergyClusterManyStrip", "hEnergyClusterManyStrip", 2000, 0, 1000);
  hEnergyClusterManyStrip->GetXaxis()->SetTitle("ADC");

  TH1F *hEnergyClusterSeed =
      new TH1F("hEnergyClusterSeed", "hEnergyClusterSeed", 2000, 0, 1000);
  hEnergyClusterSeed->GetXaxis()->SetTitle("ADC");

  TH1F *hPercentageSeed =
      new TH1F("hPercentageSeed", "hPercentageSeed", 200, 20, 100);
  hPercentageSeed->GetXaxis()->SetTitle("percentage");

  TH1F *hPercSeedintegral =
      new TH1F("hPercSeedintegral", "hPercSeedintegral", 200, 20, 100);
  hPercSeedintegral->GetXaxis()->SetTitle("percentage");

  TH1F *hClusterCharge =
      new TH1F("hClusterCharge", "hClusterCharge", 1000, -0.5, 5.5);
  hClusterCharge->GetXaxis()->SetTitle("Charge");

  TH1F *hSeedCharge = new TH1F("hSeedCharge", "hSeedCharge", 1000, -0.5, 5.5);
  hSeedCharge->GetXaxis()->SetTitle("Charge");

  TH1F *hClusterSN = new TH1F("hClusterSN", "hClusterSN", 1000, 0, 500);
  hClusterSN->GetXaxis()->SetTitle("S/N");

  TH1F *hSeedSN = new TH1F("hSeedSN", "hSeedSN", 1000, 0, 500);
  hSeedSN->GetXaxis()->SetTitle("S/N");

  TH1F *hClusterCog = new TH1F("hClusterCog", "hClusterCog", (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5);
  hClusterCog->GetXaxis()->SetTitle("cog");

  TH1F *hSeedPos = new TH1F("hSeedPos", "hSeedPos", (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5);
  hSeedPos->GetXaxis()->SetTitle("strip");

  TH1F *hNclus = new TH1F("hclus", "hclus", 10, -0.5, 9.5);
  hNclus->GetXaxis()->SetTitle("n clusters");

  TH1F *hNstrip = new TH1F("hNstrip", "hNstrip", 10, -0.5, 9.5);
  hNstrip->GetXaxis()->SetTitle("n strips");

  TH1F *hNstripSeed = new TH1F("hNstripSeed", "hNstripSeed", 10, -0.5, 9.5);
  hNstripSeed->GetXaxis()->SetTitle("n strips over seed threshold");

  TH1F *hEta = new TH1F("hEta", "hEta", 100, -1, 1);
  hEta->GetXaxis()->SetTitle("Eta");

  TH1F *hDifference = new TH1F("hDifference", "hDifference", 200, -50, 50);
  hDifference->GetXaxis()->SetTitle("Difference");

  TH2F *hADCvsWidth =
      new TH2F("hADCvsWidth", "hADCvsWidth", 10, -0.5, 9.5, 2000, 0, 1000);
  hADCvsWidth->GetXaxis()->SetTitle("# of strips");
  hADCvsWidth->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsPos = new TH2F("hADCvsPos", "hADCvsPos", (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5,
                             2000, 0, 1000);
  hADCvsPos->GetXaxis()->SetTitle("cog");
  hADCvsPos->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsEta =
      new TH2F("hADCvsEta", "hADCvsEta", 200, -1, 1, 2000, 0, 1000);
  hADCvsEta->GetXaxis()->SetTitle("eta");
  hADCvsEta->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsSN = new TH2F("hADCvsSN", "hADCvsSN", 1000, 0, 500, 2000, 0, 1000);
  hADCvsSN->GetXaxis()->SetTitle("S/N");
  hADCvsSN->GetYaxis()->SetTitle("ADC");

  TH2F *hNStripvsSN =
      new TH2F("hNstripvsSN", "hNstripvsSN", 1000, 0, 500, 5, -0.5, 4.5);
  hNStripvsSN->GetXaxis()->SetTitle("S/N");
  hNStripvsSN->GetYaxis()->SetTitle("# of strips");

  TH1F *hCommonNoise = new TH1F("hCommonNoise", "hCommonNoise", 100, -20, 20);
  hCommonNoise->GetXaxis()->SetTitle("CN");

  TH2F *hCommonNoiseVsVA = new TH2F("hCommonNoiseVsVA", "hCommonNoiseVsVA", 100, -20, 20, 10, -0.5, 9.5);
  hCommonNoiseVsVA->GetXaxis()->SetTitle("CN");
  hCommonNoiseVsVA->GetYaxis()->SetTitle("VA");

  if (argc < 10)
  {
    std::cout
        << "Usage:\n ./miniTRB_clusterize <output_rootfile> <calibration file> "
           "<high threshold> <low threshold> <symmetric clusters> <symmetric "
           "clusters width> <use absolute thresholds> <common noise type> "
           "<first input root-filename> [second input root-filename] ..."
        << std::endl;
    return 1;
  }

  int maxCN = 999;

  // Join ROOTfiles in a single chain
  TChain *chain = new TChain("raw_events"); //Chain input rootfiles
  for (int ii = 9; ii < argc; ii++)
  {
    std::cout << "Adding file " << argv[ii] << " to the chain..." << std::endl;
    chain->Add(argv[ii]);
  }

  int entries = chain->GetEntries();
  std::cout << "This run has " << entries << " entries" << std::endl;

  // Read raw event from input chain TTree
  std::vector<unsigned short> *raw_event = 0;
  TBranch *RAW = 0;
  chain->SetBranchAddress("RAW Event", &raw_event, &RAW);

  // Create output ROOTfile
  TString output_filename = argv[1];
  TFile *foutput = new TFile(output_filename.Data(), "RECREATE");
  foutput->cd();

  //Read Calibration file
  calib cal;
  read_calib(argv[2], &cal);

  // Loop over events
  int perc = 0;
  for (int index_event = 0; index_event < entries; index_event++)
  {
    chain->GetEntry(index_event);

    if (verbose)
    {
      std::cout << std::endl;
      std::cout << "EVENT: " << index_event << std::endl;
    }

    Double_t pperc = 10.0 * ((index_event + 1.0) / entries);
    if (pperc >= perc)
    {
      std::cout << "Processed " << (index_event + 1) << " out of " << entries
                << ":" << (int)(100.0 * (index_event + 1.0) / entries) << "%"
                << std::endl;
      perc++;
    }

    std::vector<float> signal(raw_event->size()); //Vector of pedestal subtracted signal
    std::vector<cluster> result;                  //Vector of resulting clusters

    if (raw_event->size() == 384 || raw_event->size() == 640)
    {
      if (cal.ped.size() >= raw_event->size())
      {
        for (size_t i = 0; i != raw_event->size(); i++)
        {
          signal.at(i) = (raw_event->at(i) - cal.ped[i]);
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
    else
    {
      if (verbose)
      {
        std::cout << "Error: event " << index_event << " is not complete, skipping it" << std::endl;
      }
      continue;
    }

    if (atoi(argv[8]) >= 0)
    {
#pragma omp parallel for                //Multithread for loop
      for (int va = 0; va < NVas; va++) //Loop on VA
      {
        float cn = GetCN(&signal, va, atoi(argv[8]));
        if (cn != -999 && abs(cn) < maxCN)
        {
          hCommonNoise->Fill(cn);
          hCommonNoiseVsVA->Fill(cn, va);

          for (int ch = va * 64; ch < (va + 1) * 64; ch++) //Loop on VA channels
          {
            signal.at(ch) = signal.at(ch) - cn;
          }
        }
        else
        {
          for (int ch = va * 64; ch < (va + 1) * 64; ch++)
          {
            signal.at(ch) = 0; //Invalid Common Noise Value, artificially setting VA channel to 0 signal
          }
        }
      }
    }

    try
    {

      result = clusterize(&cal, &signal, atof(argv[3]), atof(argv[4]),
                          atoi(argv[5]), atoi(argv[6]), atoi(argv[7]));

      for (int i = 0; i < result.size(); i++)
      {
        if (verbose)
        {
          PrintCluster(result.at(i));
        }

        if (!GoodCluster(result.at(i), &cal))
          continue;

        if (result.at(i).address >= minStrip && (result.at(i).address + result.at(i).width - 1) < maxStrip)
        {
          if (i == 0)
          {
            hNclus->Fill(result.size());
          }

          hEnergyCluster->Fill(GetClusterSignal(result.at(i)));

          if (result.at(i).width == 1)
          {
            hEnergyCluster1Strip->Fill(GetClusterSignal(result.at(i)));
          }
          else if (result.at(i).width == 2)
          {
            hEnergyCluster2Strip->Fill(GetClusterSignal(result.at(i)));
          }
          else
          {
            hEnergyClusterManyStrip->Fill(GetClusterSignal(result.at(i)));
          }

          hEnergyClusterSeed->Fill(GetClusterSeedADC(result.at(i), &cal));
          hClusterCharge->Fill(GetClusterMIPCharge(result.at(i)));
          hSeedCharge->Fill(GetSeedMIPCharge(result.at(i), &cal));
          hPercentageSeed->Fill(100 * GetClusterSeedADC(result.at(i), &cal) / GetClusterSignal(result.at(i)));
          hClusterSN->Fill(GetClusterSN(result.at(i), &cal));
          hSeedSN->Fill(GetSeedSN(result.at(i), &cal));

          if (verbose)
          {
            std::cout << "Adding cluster with COG: " << GetClusterCOG(result.at(i)) << std::endl;
          }

          hClusterCog->Fill(GetClusterCOG(result.at(i)));
          hSeedPos->Fill(GetClusterSeed(result.at(i), &cal));
          hNstrip->Fill(GetClusterWidth(result.at(i)));
          hEta->Fill(GetClusterEta(result.at(i)));
          hADCvsWidth->Fill(GetClusterWidth(result.at(i)), GetClusterSignal(result.at(i)));
          hADCvsPos->Fill(GetClusterCOG(result.at(i)), GetClusterSignal(result.at(i)));
          hADCvsEta->Fill(GetClusterEta(result.at(i)), GetClusterSignal(result.at(i)));
          hADCvsSN->Fill(GetClusterSN(result.at(i), &cal), GetClusterSignal(result.at(i)));
          hNStripvsSN->Fill(GetClusterSN(result.at(i), &cal), GetClusterWidth(result.at(i)));
          hNstripSeed->Fill(result.at(i).over);

          if (result.at(i).width == 2)
          {
            hDifference->Fill(result.at(i).ADC.at(0) - result.at(i).ADC.at(1));
          }
        }
      }
    }
    catch (const char *msg)
    {
      if (verbose)
      {
        std::cerr << msg << "Skipping event " << index_event << std::endl;
      }
      hNclus->Fill(0);
      continue;
    }
  }

  hNclus->Write();
  hEnergyCluster->Write();
  hEnergyCluster1Strip->Write();
  hEnergyCluster2Strip->Write();
  hEnergyClusterManyStrip->Write();
  hEnergyClusterSeed->Write();
  hClusterCharge->Write();
  hSeedCharge->Write();
  hClusterSN->Write();
  hSeedSN->Write();
  hClusterCog->Write();
  hSeedPos->Write();
  hNstrip->Write();
  hNstripSeed->Write();
  hEta->Write();
  hADCvsWidth->Write();
  hADCvsPos->Write();
  hADCvsEta->Write();
  hADCvsSN->Write();
  hNStripvsSN->Write();
  hDifference->Write();
  hCommonNoise->Write();
  hCommonNoiseVsVA->Write();

  foutput->Close();
  return 0;
}
