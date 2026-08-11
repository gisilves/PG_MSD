#include "TROOT.h"
#include "TSystem.h"
#include "TChain.h"
#include "TFile.h"
#include "TF1.h"
#include "TH1.h"
#include "TH2.h"
#include "TGraph.h"
#include "TTree.h"
#include "TKey.h"
#include <iostream>
#include <algorithm>
#include <vector>
#include <cmath>

#include "TTreeReader.h"

#include <CLI/CLI.hpp>
#include "event.h"

calib update_pedestals(TH1D **hADC, int NChannels, calib cal)
// Dynamic pedestal calculation while processing the file:
// when used it is assumed that the single strip occupancy will be low (not true for an higly collimated beam)
{
  calib new_calibration; // calibration struct

  std::vector<float> pedestals; // vector of pedestals
  float mean_pedestal = 0;
  float rms_pedestal = 0;
  std::vector<float> rsigma; // vector of strip noise
  float mean_rsigma = 0;
  float rms_rsigma = 0;
  std::vector<float> sigma; // vector of strip noise after common mode subtraction
  float mean_sigma = 0;
  float rms_sigma = 0;

  TF1 *fittedgaus;

  for (int ch = 0; ch < NChannels; ch++)
  {
    // Fitting histos with gaus to compute ped and raw_sigma: it is assumed that channel noise is normal (true unless there is a problem with the readout ASIC)
    if (hADC[ch]->GetEntries())
    {
      hADC[ch]->Fit("gaus", "QS");
      fittedgaus = (TF1 *)hADC[ch]->GetListOfFunctions()->FindObject("gaus");
      pedestals.push_back(fittedgaus->GetParameter(1)); // mean of the fitted gaussian is the pedestal for the channel
      rsigma.push_back(fittedgaus->GetParameter(2));    // sigma of the fitted gaussian is the noise for the channel
    }
    else
    {
      pedestals.push_back(0); // there was no data to compute pedestals and noise (channel disabled): setting them to 0
      rsigma.push_back(0);
    }
  }

  new_calibration = (calib){.ped = pedestals, .rsig = rsigma, .sig = cal.sig, .status = cal.status}; // new calibration structure has the updated pedestals, we use previous info for all the other parameters
  return new_calibration;
}

int clusterize_detector(int board, int minADC_h, int maxADC_h, int minStrip, int maxStrip,
                        bool newDAQ, int first_event, int NChannels, bool verb, bool dynped,
                        bool invert, float maxCN, int cntype, int NVas,
                        float highthreshold, float lowthreshold, bool absolute,
                        bool symmetric, int symmetricwidth,
                        int sensor_pitch, int version, std::vector<std::string> input_files, int nevents = -1, std::string calibration_file = "", bool inVA = false, bool only_highest = false)
{
  //////////////////Histos//////////////////
  TH1F *hADCCluster = // ADC content of all clusters
      new TH1F((TString) "hADCCluster_board_" + board, (TString) "hADCCluster_board_" + board, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCCluster->GetXaxis()->SetTitle("ADC");

  TH1F *hHighestStrip = // ADC of highest signal
      new TH1F((TString) "hHighestStrip_board_" + board, (TString) "hHighestStrip_board_" + board, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hHighestStrip->GetXaxis()->SetTitle("ADC");

  TH1F *hADCClusterEdge = // ADC content of all clusters
      new TH1F((TString) "hADCClusterEdge_board_" + board, (TString) "hADCClusterEdge_board_" + board, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCClusterEdge->GetXaxis()->SetTitle("ADC");

  TH1F *hADCCluster1Strip = // ADC content of clusters with a single strips
      new TH1F((TString) "hADCCluster1Strip_board_" + board, (TString) "hADCCluster1Strip_board_" + board, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCCluster1Strip->GetXaxis()->SetTitle("ADC");

  TH1F *hADCCluster2Strip = // ADC content of clusters with 2 strips
      new TH1F((TString) "hADCCluster2Strip_board_" + board, (TString) "hADCCluster2Strip_board_" + board, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCCluster2Strip->GetXaxis()->SetTitle("ADC");

  TH1F *hADCClusterManyStrip = // ADC content of clusters with more than 2 strips
      new TH1F((TString) "hADCClusterManyStrip_board_" + board, (TString) "hADCClusterManyStrip_board_" + board, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCClusterManyStrip->GetXaxis()->SetTitle("ADC");

  TH1F *hADCClusterSeed = // ADC content of the "seed strip"
      new TH1F((TString) "hADCClusterSeed_board_" + board, (TString) "hADCClusterSeed_board_" + board, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCClusterSeed->GetXaxis()->SetTitle("ADC");

  TH1F *hPercentageSeed = // percentage of the "seed strip" wrt the whole cluster
      new TH1F((TString) "hPercentageSeed_board_" + board, (TString) "hPercentageSeed_board_" + board, 200, 20, 150);
  hPercentageSeed->GetXaxis()->SetTitle("percentage");

  TH1F *hPercSeedintegral =
      new TH1F((TString) "hPercSeedintegral_board_" + board, (TString) "hPercSeedintegral_board_" + board, 200, 20, 150);
  hPercSeedintegral->GetXaxis()->SetTitle("percentage");

  TH1F *hClusterCharge = // sqrt(ADC signal / MIP_ADC) for the cluster
      new TH1F((TString) "hClusterCharge_board_" + board, (TString) "hClusterCharge_board_" + board, 1000, -0.5, 25.5);
  hClusterCharge->GetXaxis()->SetTitle("Charge");

  TH1F *hSeedCharge = new TH1F((TString) "hSeedCharge_board_" + board, (TString) "hSeedCharge_board_" + board, 1000, -0.5, 25.5); // sqrt(ADC signal / MIP_ADC) for the seed
  hSeedCharge->GetXaxis()->SetTitle("Charge");

  TH1F *hClusterSN = new TH1F((TString) "hClusterSN_board_" + board, (TString) "hClusterSN_board_" + board, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h); // cluster S/N
  hClusterSN->GetXaxis()->SetTitle("S/N");

  TH1F *hSeedSN = new TH1F((TString) "hSeedSN_board_" + board, (TString) "hSeedSN_board_" + board, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h); // seed S/N
  hSeedSN->GetXaxis()->SetTitle("S/N");

  TH1F *hClusterCog = new TH1F((TString) "hClusterCog_board_" + board, (TString) "hClusterCog_board_" + board, (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5); // clusters center of gravity in terms of strip number
  hClusterCog->GetXaxis()->SetTitle("cog");

  TH1F *hBeamProfile = new TH1F((TString) "hBeamProfile_board_" + board, (TString) "hBeamProfile_board_" + board, 100, -0.5, 99.5); // clusters center of gravity converted to mm
  hBeamProfile->GetXaxis()->SetTitle("pos (mm)");

  TH1F *hSeedPos = new TH1F((TString) "hSeedPos_board_" + board, (TString) "hSeedPos_board_" + board, (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5); // clusters seed position in terms of strip number
  hSeedPos->GetXaxis()->SetTitle("strip");

  TH1F *hNclus = new TH1F((TString) "hclus_board_" + board, (TString) "hclus_board_" + board, 10, -0.5, 9.5); // number of clusters found in each event
  hNclus->GetXaxis()->SetTitle("n clusters");

  TH1F *hNstrip = new TH1F((TString) "hNstrip_board_" + board, (TString) "hNstrip_board_" + board, 10, -0.5, 9.5); // number of strips per cluster
  hNstrip->GetXaxis()->SetTitle("n strips");

  TH1F *hNstripSeed = new TH1F((TString) "hNstripSeed_board_" + board, (TString) "hNstripSeed_board_" + board, 10, -0.5, 9.5);
  hNstripSeed->GetXaxis()->SetTitle("n strips over seed threshold");

  TH2F *hADCvsSeed = new TH2F((TString) "hADCvsSeed_board_" + board, (TString) "hADCvsSeed_board_" + board, 1000, 0, 500, // cluster ADC vs seed ADC
                              1000, 0, 500);
  hADCvsSeed->GetXaxis()->SetTitle("ADC Seed");
  hADCvsSeed->GetYaxis()->SetTitle("ADC Tot");

  TH1F *hEta = new TH1F((TString) "hEta_board_" + board, (TString) "hEta_board_" + board, 100, 0, 1);
  hEta->GetXaxis()->SetTitle("Eta");

  TH1F *hEta1 = new TH1F((TString) "hEta1_board_" + board, (TString) "hEta1_board_" + board, 100, 0, 1);
  hEta1->GetXaxis()->SetTitle("Eta (one seed)");

  TH1F *hEta2 = new TH1F((TString) "hEta2_board_" + board, (TString) "hEta2_board_" + board, 100, 0, 1);
  hEta2->GetXaxis()->SetTitle("Eta (two seed)");

  TH1F *hDifference = new TH1F((TString) "hDifference_board_" + board, (TString) "hDifference_board_" + board, 200, -5, 5); // relative difference for clusters with 2 strips
  hDifference->GetXaxis()->SetTitle("(ADC_0-ADC_1)/(ADC_0+ADC_1)");

  TH2F *hADCvsWidth = // cluster ADC vs cluster width
      new TH2F((TString) "hADCvsWidth_board_" + board, (TString) "hADCvsWidth_board_" + board, 10, -0.5, 9.5, 1000, 0, 500);
  hADCvsWidth->GetXaxis()->SetTitle("# of strips");
  hADCvsWidth->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsPos = new TH2F((TString) "hADCvsPos_board_" + board, (TString) "hADCvsPos_board_" + board, (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5, // cluster ADC vs cog
                             1000, minADC_h, maxADC_h);

  hADCvsPos->GetXaxis()->SetTitle("cog");
  hADCvsPos->GetYaxis()->SetTitle("ADC");

  TH2F *hSeedADCvsPos = new TH2F((TString) "hSeedADCvsPos_board_" + board, (TString) "hSeedADCvsPos_board_" + board, (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5, // cluster ADC vs cog
                             1000, minADC_h, maxADC_h);

  hSeedADCvsPos->GetXaxis()->SetTitle("cog");
  hSeedADCvsPos->GetYaxis()->SetTitle("ADC");

  TH2F* hClusterChargevsPos = new TH2F((TString) "hClusterChargevsPos_board_" + board, (TString) "hClusterChargevsPos_board_" + board, (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5, // cluster ADC vs cog
                             1000, minADC_h, maxADC_h);

  hClusterChargevsPos->GetXaxis()->SetTitle("cog");
  hClusterChargevsPos->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsEta = // ignore
      new TH2F((TString) "hADCvsEta_board_" + board, (TString) "hADCvsEta_board_" + board, 200, 0, 1, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCvsEta->GetXaxis()->SetTitle("eta");
  hADCvsEta->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsSN = new TH2F((TString) "hADCvsSN_board_" + board, (TString) "hADCvsSN_board_" + board, 2000, 0, 2500, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCvsSN->GetXaxis()->SetTitle("S/N");
  hADCvsSN->GetYaxis()->SetTitle("ADC");

  TH2F *hNStripvsSN =
      new TH2F((TString) "hNstripvsSN_board_" + board, (TString) "hNstripvsSN_board_" + board, 1000, 0, 2500, 5, -0.5, 4.5);
  hNStripvsSN->GetXaxis()->SetTitle("S/N");
  hNStripvsSN->GetYaxis()->SetTitle("# of strips");

  TH1F *hCommonNoise0 = new TH1F((TString) "hCommonNoise0_board_" + board, (TString) "hCommonNoise0_board_" + board, 100, -20, 20); // common noise: first algo
  hCommonNoise0->GetXaxis()->SetTitle("CN");

  TH1F *hCommonNoise1 = new TH1F((TString) "hCommonNoise1_board_" + board, (TString) "hCommonNoise1_board_" + board, 100, -20, 20); // common noise: second algo
  hCommonNoise1->GetXaxis()->SetTitle("CN");

  TH1F *hCommonNoise2 = new TH1F((TString) "hCommonNoise2_board_" + board, (TString) "hCommonNoise2_board_" + board, 100, -20, 20); // common noise: third algo
  hCommonNoise2->GetXaxis()->SetTitle("CN");

  TH2F *hCommonNoiseVsVA = new TH2F((TString) "hCommonNoiseVsVA_board_" + board, (TString) "hCommonNoiseVsVA_board_" + board, 100, -20, 20, 10, -0.5, 9.5);
  hCommonNoiseVsVA->GetXaxis()->SetTitle("CN");
  hCommonNoiseVsVA->GetYaxis()->SetTitle("VA");

  TH2F *hEtaVsADC = new TH2F((TString) "hEtaVsADC_board_" + board, (TString) "hEtaVsADC_board_" + board, 100, 0, 1, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hCommonNoiseVsVA->GetXaxis()->SetTitle("ADC");
  hCommonNoiseVsVA->GetYaxis()->SetTitle("Eta");

  TH2F *hADC0vsADC1 = new TH2F((TString) "hADC0vsADC1_board_" + board, (TString) "hADC0vsADC1_board_" + board, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h); // ADC of first strip vs ADC of second strip for clusters with 2 strips
  hADC0vsADC1->GetXaxis()->SetTitle("ADC0");
  hADC0vsADC1->GetYaxis()->SetTitle("ADC1");

  TGraph *nclus_event = new TGraph(); // number of clusters as a function of event number
  nclus_event->SetName((TString) "nclus_event_board_" + board);
  nclus_event->SetTitle((TString) "nclus_event_board_" + board);

  // Join ROOTfiles in a single chain
  TChain *chain = new TChain();

  if (board == 0) // TTree name depends on DAQ board
  {
    std::cout << "\nWe are on the first detector" << std::endl;
    chain->SetName("raw_events");
    
    for (int ii = 0; ii < input_files.size(); ii++)
    {
      std::cout << "\nAdding file " << input_files[ii] << " to the chain..." << std::endl;
      chain->Add(input_files[ii].c_str());
    }
  }
  else
  {
    std::cout << "\nWe are on detector " << board << std::endl;
    std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    chain->SetName((TString) "raw_events_" + alphabet.at(board));
    for (int ii = 0; ii < input_files.size(); ii++)
    {
      std::cout << "\nAdding file " << input_files[ii] << " to the chain..." << std::endl;
      chain->Add(input_files[ii].c_str());
    }
  }

  int entries = chain->GetEntries();

  if (nevents) // to process only the first "nevents" events in the chain 
  {
    unsigned int temp_entries = nevents;
    if (temp_entries < entries)
    {
      entries = temp_entries;
    }
  }

  if (entries == 0)
  {
    std::cout << "Error: no file or empty file" << std::endl;
    return 2;
  }
  std::cout << "\nThis run has " << entries << " entries" << std::endl;

  if (first_event > entries)
  {
    std::cout << "Error: first event is greater than the number of entries" << std::endl;
    return 2;
  }

  std::vector<unsigned int> *raw_event = 0; // buffer vector for the raw event in the TTree
  TBranch *RAW = 0;

  std::cout << "\nProcessing board " << board << std::endl;
  chain->SetBranchAddress("RAW Event", &raw_event, &RAW);

  std::vector<cluster> result; // Vector of resulting clusters

  // add t_clusters TTree to output file with name containing board
  TString tree_name = "t_clusters_board_" + std::to_string(board);
  TTree *t_clusters = new TTree(tree_name, tree_name);
  t_clusters->Branch("clusters", &result);

  // Read Calibration file
  if (!calibration_file.size())
  {
    std::cout << "Error: no calibration file" << std::endl;
    return 2;
  }

  calib cal; // calibration struct
  bool is_calib = false;

  is_calib = read_calib(calibration_file.c_str(), &cal, NChannels, board, verb);

  if (!is_calib)
  {
    std::cout << "ERROR: no calibration file found" << std::endl;
    return 2;
  }

  // histos for dynamic calibration
  TH1D *hADC[NChannels];
  for (int ch = 0; ch < NChannels; ch++)
  {
    hADC[ch] = new TH1D(Form("pedestal_channel_%d_board_%d", ch, board), Form("Pedestal %d", ch), 50, 0, -1);
    hADC[ch]->GetXaxis()->SetTitle("ADC");
  }

  // Loop over events
  int perc = 0;   // percentage of processed events
  int maxADC = 0; // max ADC in all the events, to set proper graph/histo limits
  int maxEVT = 0; // event where maxADC was found
  int maxPOS = 0; // position of the strip with value maxADC
  bool AMS = false;

  std::cout << "\n===========================================================" << std::endl;
  std::cout << "\nProcessing events for board " << board << std::endl;

  std::cout << "\nProcessing " << entries << " entries, starting from event " << first_event << std::endl;

  bool BL_monster = false;
  if (version == 2023 || version == 2024)
  {
    AMS = true;
    if (version == 2024)
    {
      BL_monster = true;
    }
  }

  for (int index_event = first_event; index_event < entries; index_event++) // looping on the events
  {
    chain->GetEntry(index_event);

    if (verb)
    {
      std::cout << std::endl;
      std::cout << "EVENT: " << index_event << std::endl;
    }
    Double_t pperc = 10.0 * ((index_event + 1.0) / entries); // print every 10% of processed events
    if (pperc >= perc)
    {
      std::cout << "Processed " << (index_event + 1) << " out of " << entries
                << ":" << (int)(100.0 * (index_event + 1.0) / entries) << "%"
                << std::endl;
      perc++;
    }

    if ((index_event % 5000) == 0 && dynped) // if dynamic pedestals are enabled we recalculate them
    {
      std::cout << "Updating pedestals" << std::endl;

      cal = update_pedestals(hADC, NChannels, cal);
      for (int ch = 0; ch < NChannels; ch++)
      {
        hADC[ch]->Reset(); // we only keep the last 5000 events for the pedestals
      }
    }

    std::vector<float> signal(raw_event->size()); // Vector of pedestal subtracted signal

    if (raw_event->size() == NChannels) // if the raw file was correctly processed these is the only possible value
    {
      if (cal.ped.size() >= raw_event->size())
      {
        for (size_t i = 0; i != raw_event->size(); i++)
        {
          if (cal.status[i] != 0)
          {
            signal.at(i) = 0; // channel has a non 0 status in calibration (problem with channel: noisy, dead etc..), setting signal to 0
          }
          else
          {

            signal.at(i) = (raw_event->at(i) - cal.ped[i]);
            if (dynped && signal.at(i) < 10) // if dynamic pedestals are enabled and signal is below 10 (probably not signal) we save the value to recalculate the pedestal
            {
              hADC[i]->Fill(raw_event->at(i));
            }

            if (invert)
            {
              signal.at(i) = -signal.at(i); // one of the prototype DAQ boards had the analog output inverted
            }
          }
        }
      }
      else
      {
        if (verb)
        {
          std::cout << "Error: calibration file is not compatible" << std::endl;
        }
      }
    }
    else
    {
      if (verb)
      {
        std::cout << "Error: event " << index_event << " is not complete, skipping it" << std::endl;
        std::cout << "Event size: " << raw_event->size() << " vs " << NChannels << std::endl;
      }
      continue;
    }

    for (int va = 0; va < NVas; va++) // Loop on VA (readout chip): common noise algo 1
    {
      float cn = GetCN(&signal, va, 0);
      if (verb)
      {
        std::cout << "VA " << va << ": " << cn << std::endl;
      }
      if (cn != -999 && abs(cn) < maxCN)
      {
        hCommonNoise0->Fill(cn);
      }
    }

    for (int va = 0; va < NVas; va++) // Loop on VA: common noise algo 2
    {
      float cn = GetCN(&signal, va, 1);
      if (cn != -999 && abs(cn) < maxCN)
      {
        hCommonNoise1->Fill(cn);
      }
    }

    for (int va = 0; va < NVas; va++) // Loop on VA: common noise algo 3
    {
      float cn = GetCN(&signal, va, 2);
      if (cn != -999 && abs(cn) < maxCN)
      {
        hCommonNoise2->Fill(cn);
      }
    }

    bool goodCN = true;
    if (cntype >= 0)
    {
      for (int va = 0; va < NVas; va++) // Loop on VA
      {
        float cn = GetCN(&signal, va, cntype);
        if (verb)
        {
          std::cout << "VA " << va << " CN " << cn << std::endl;
        }
        if (cn != -999 && abs(cn) < maxCN)
        {
          hCommonNoiseVsVA->Fill(cn, va);
          goodCN = true;

          for (int ch = va * 64; ch < (va + 1) * 64; ch++) // Loop on VA channels, subtracting common mode noise to the signals before clustering
          {
            signal.at(ch) = signal.at(ch) - cn;
          }
        }
        else
        {
          for (int ch = va * 64; ch < (va + 1) * 64; ch++)
          {
            signal.at(ch) = 0; // Invalid Common Noise Value, artificially setting VA channel to 0 signal
            goodCN = false;
          }
        }
      }
    }

    try
    {
      if (!goodCN)
        continue;

      if (*max_element(signal.begin(), signal.end()) > maxADC) // searching for the highest ADC value
      {
        maxADC = *max_element(signal.begin(), signal.end());
        maxEVT = index_event;
        std::vector<float>::iterator it = std::find(signal.begin(), signal.end(), maxADC);
        maxPOS = std::distance(signal.begin(), it);
      }

      if (verb)
        std::cout << "Highest strip: " << *max_element(signal.begin(), signal.end()) << std::endl;

      hHighestStrip->Fill(*max_element(signal.begin(), signal.end()));

      // if it's BL_monster we keep only channels 320-383, 448-639, deleting the others from the vector
      if (BL_monster)
      {
        signal.erase(signal.begin(), signal.begin() + 320);
        signal.erase(signal.begin() + 64, signal.begin() + 128);
        signal.erase(signal.begin() + 256, signal.end());
      }

      result = clusterize_event(&cal, &signal, highthreshold, lowthreshold, // clustering function
                                symmetric, symmetricwidth, absolute, board, verb);

      if (inVA)
      {
        // remove clusters that are not fully contained in a VA
        result.erase(std::remove_if(result.begin(), result.end(), [](cluster c) { return !isClusterinVA(c); }), result.end());
      }

      // save result cluster in TTree
      t_clusters->Fill();

      if (only_highest && result.size() > 1)
      {
        // only keep the first cluster
        result.erase(result.begin() + 1, result.end());
      }

      nclus_event->SetPoint(nclus_event->GetN(), index_event, result.size());
      hNclus->Fill(result.size());

      for (int i = 0; i < result.size(); i++)
      {

        if (verb)
        {
          PrintCluster(result.at(i));
        }

        if (result.at(i).address >= minStrip && (result.at(i).address + result.at(i).width - 1) < maxStrip) // cut on position on the detector in terms of strip number
        {

          hADCCluster->Fill(GetClusterSignal(result.at(i)));

          if (GetClusterSeed(result.at(i), &cal) % 64 == 0)
          {
            hADCClusterEdge->Fill(GetClusterSignal(result.at(i)));
          }

          if (result.at(i).width == 1)
          {
            hADCCluster1Strip->Fill(GetClusterSignal(result.at(i)));
            hEtaVsADC->Fill(GetClusterEta(result.at(i)), GetClusterSignal(result.at(i)));
          }
          else if (result.at(i).width == 2)
          {
            hADCCluster2Strip->Fill(GetClusterSignal(result.at(i)));
            hEtaVsADC->Fill(GetClusterEta(result.at(i)), GetClusterSignal(result.at(i)));
          }
          else
          {
            hADCClusterManyStrip->Fill(GetClusterSignal(result.at(i)));
            hEtaVsADC->Fill(GetClusterEta(result.at(i)), GetClusterSignal(result.at(i)));
          }

          hADCClusterSeed->Fill(GetClusterSeedADC(result.at(i), &cal));
          hClusterCharge->Fill(GetClusterMIPCharge(result.at(i)));
          hSeedCharge->Fill(GetSeedMIPCharge(result.at(i), &cal));
          hPercentageSeed->Fill(100 * GetClusterSeedADC(result.at(i), &cal) / GetClusterSignal(result.at(i)));
          hClusterSN->Fill(GetClusterSN(result.at(i), &cal));
          hSeedSN->Fill(GetSeedSN(result.at(i), &cal));

          if (verb)
          {
            std::cout << "Adding cluster with COG: " << GetClusterCOG(result.at(i)) << std::endl;
          }

          hClusterCog->Fill(GetClusterCOG(result.at(i)));
          hBeamProfile->Fill(GetPosition(result.at(i), sensor_pitch));
          hSeedPos->Fill(GetClusterSeed(result.at(i), &cal));
          hNstrip->Fill(GetClusterWidth(result.at(i)));

          if (result.at(i).width)
          {
            hEta->Fill(GetClusterEta(result.at(i)));
            if (result.at(i).over == 1)
            {
              hEta1->Fill(GetClusterEta(result.at(i)));
            }
            else
            {
              hEta2->Fill(GetClusterEta(result.at(i)));
            }
            hADCvsEta->Fill(GetClusterEta(result.at(i)), GetClusterSignal(result.at(i)));
          }

          hADCvsWidth->Fill(GetClusterWidth(result.at(i)), GetClusterSignal(result.at(i)));
          hADCvsPos->Fill(GetClusterCOG(result.at(i)), GetClusterSignal(result.at(i)));
          hSeedADCvsPos->Fill(GetClusterSeed(result.at(i), &cal), GetClusterSeedADC(result.at(i), &cal));
          hClusterChargevsPos->Fill(GetClusterCOG(result.at(i)), GetClusterMIPCharge(result.at(i)));
          hADCvsSeed->Fill(GetClusterSeedADC(result.at(i), &cal), GetClusterSignal(result.at(i)));
          hADCvsSN->Fill(GetClusterSN(result.at(i), &cal), GetClusterSignal(result.at(i)));
          hNStripvsSN->Fill(GetClusterSN(result.at(i), &cal), GetClusterWidth(result.at(i)));
          hNstripSeed->Fill(result.at(i).over);

          if (result.at(i).width == 2)
          {
            hDifference->Fill((result.at(i).ADC.at(0) - result.at(i).ADC.at(1)) / (result.at(i).ADC.at(0) + result.at(i).ADC.at(1)));
            hADC0vsADC1->Fill(result.at(i).ADC.at(0), result.at(i).ADC.at(1));
          }
        }
      }
    }
    catch (const char *msg)
    {
      if (verb)
      {
        std::cerr << msg << "Skipping event " << index_event << std::endl;
      }
      hNclus->Fill(0);
      continue;
    }
  }

  if (verb)
  {
    std::cout << "Maximum ADC value found is " << maxADC
              << " in event number " << maxEVT
              << " at strip " << maxPOS << std::endl;
  }

  hNclus->Write();
  delete hNclus;

  hADCCluster->Write();
  delete hADCCluster;

  hHighestStrip->Write();
  delete hHighestStrip;

  hADCClusterEdge->Write();
  delete hADCClusterEdge;

  hADCCluster1Strip->Write();
  delete hADCCluster1Strip;

  hADCCluster2Strip->Write();
  delete hADCCluster2Strip;

  hADCClusterManyStrip->Write();
  delete hADCClusterManyStrip;

  hEtaVsADC->Write();
  delete hEtaVsADC;

  hADCClusterSeed->Write();
  hClusterCharge->Write();
  hSeedCharge->Write();
  hClusterSN->Write();
  hSeedSN->Write();
  hClusterCog->Write();
  hBeamProfile->Write();
  hSeedPos->Write();
  hNstrip->Write();
  hNstripSeed->Write();
  hEta->Write();
  hEta1->Write();
  hEta2->Write();
  hADCvsWidth->Write();
  hADCvsPos->Write();
  hSeedADCvsPos->Write();
  hClusterChargevsPos->Write();
  hADCvsSeed->Write();
  hADCvsEta->Write();
  hADCvsSN->Write();
  hNStripvsSN->Write();
  hDifference->Write();
  hADC0vsADC1->Write();
  hCommonNoise0->Write();
  hCommonNoise1->Write();
  hCommonNoise2->Write();
  hCommonNoiseVsVA->Write();

  delete hADCClusterSeed;
  delete hClusterCharge;
  delete hSeedCharge;
  delete hClusterSN;
  delete hSeedSN;
  delete hClusterCog;
  delete hBeamProfile;
  delete hSeedPos;
  delete hNstrip;
  delete hNstripSeed;
  delete hEta;
  delete hEta1;
  delete hEta2;
  delete hADCvsWidth;
  delete hADCvsPos;
  delete hSeedADCvsPos;
  delete hClusterChargevsPos;
  delete hADCvsSeed;
  delete hADCvsEta;
  delete hADCvsSN;
  delete hNStripvsSN;
  delete hDifference;
  delete hADC0vsADC1;
  delete hCommonNoise0;
  delete hCommonNoise1;
  delete hCommonNoise2;
  delete hCommonNoiseVsVA;

  nclus_event->SetTitle((TString) "nClus vs nEvent_board_" + board);
  nclus_event->GetXaxis()->SetTitle("# event");
  nclus_event->GetYaxis()->SetTitle("# clusters");
  nclus_event->SetMarkerColor(kRed + 1);
  nclus_event->SetLineColor(kRed + 1);
  nclus_event->SetMarkerSize(0.5);
  nclus_event->Draw("*lSAME");
  nclus_event->Write();
  delete nclus_event;

  t_clusters->Write();
  delete t_clusters;

  return 0;
}

int main(int argc, char *argv[])
{
  // generating shared library for cluster saving
  std::cout << "\n==========================================================================================================" << std::endl;
  std::cout << "========================================  Raw Clusterizer  ===============================================" << std::endl;
  std::cout << "==========================================================================================================" << std::endl;

  TString command;
  // check if types_C.so library is present
  if (access("./src/types_C.so", F_OK) == -1)
  {
    std::cout << "types_C.so library not found, compiling it ..." << std::endl;
    command = TString(".L ") + gSystem->pwd() + TString("/src/types.C+");
  }
  else
  {
    std::cout << "types_C.so library found, loading it ..." << std::endl;
    command = TString(".L ") + gSystem->pwd() + TString("/src/types_C.so");
  }

  gROOT->ProcessLine(command);

  gErrorIgnoreLevel = kWarning;
  bool symmetric = false;
  bool absolute = false;
  bool verb = false;
  bool invert = false;
  bool dynped = false;
  bool inVA = false;
  bool only_highest = false;

  float highthreshold = 3.5;
  float lowthreshold = 1.0;
  int symmetricwidth = 0;
  int cntype = 0;
  int maxCN = 999;
  int first_event = 0;
  int nevents = -1;
  int version = 0;
  
  int NChannels = 1792;
  int NVas = 28;
  int minStrip = 0;
  int maxStrip = 1791;
  float sensor_pitch = 0.108;
  int minADC_h = 0;
  int maxADC_h = 1000;
  
  bool newDAQ = false;
  
  int board = 0;

  std::vector<std::string> input_files;
  std::string calibration_file, output_file;

  CLI::App app{"raw_clusterize"};

  // Flags
  app.add_flag("-v,--verbose", verb, "Verbose output");
  app.add_flag("-s,--symmetric", symmetric, "Use symmetric cluster instead of double threshold");
  app.add_flag("-a,--absolute", absolute, "Use absolute ADC value instead of S/N");
  app.add_flag("--invert", invert, "Invert signal");
  app.add_flag("--dynped", dynped, "Enable dynamic pedestals");
  app.add_flag("--newDAQ", newDAQ, "Use new DAQ format");
  app.add_flag("--inVA", inVA, "Select only clusters that are fully contained in a VA");
  app.add_flag("--highest", only_highest, "Only add highest cluster to histograms");

  // Options
  app.add_option("--highthreshold", highthreshold, "High threshold for clustering");
  app.add_option("--lowthreshold", lowthreshold, "Low threshold for clustering");
  app.add_option("--symmetricwidth", symmetricwidth, "Symmetric cluster width");
  app.add_option("--cntype", cntype, "Clusterizer type");
  app.add_option("--maxCN", maxCN, "Max number of clusters");
  app.add_option("--NChannels", NChannels, "Number of channels");
  app.add_option("--NVas", NVas, "Number of VA chips");
  app.add_option("--minStrip", minStrip, "Minimum strip index");
  app.add_option("--maxStrip", maxStrip, "Maximum strip index");
  app.add_option("--sensor_pitch", sensor_pitch, "Sensor pitch in mm");
  app.add_option("--board", board, "Board number");
  app.add_option("--calibration_file", calibration_file, "Path to calibration file")->check(CLI::ExistingFile);
  app.add_option("--output_file", output_file, "Output file name");
  app.add_option("--nevents", nevents, "Number of events to process");
  app.add_option("--first_event", first_event, "First event to process");
  app.add_option("--input_files", input_files, "Input ROOT files")->required()->expected(-1);
  app.add_option("--maxADC_h", maxADC_h, "Maximum ADC value for histograms");
  app.add_option("--minADC_h", minADC_h, "Minimum ADC value for histograms");

  CLI11_PARSE(app, argc, argv);

  // Create output ROOTfile
  TString output_filename;
  if (!output_file.size())
  {
    std::cout << "Error: no output file" << std::endl;
    return 2;
  }
  else
  {
    output_filename = TString(output_file);
    std::cout << "Output file: " << output_filename << std::endl;
  }

  std::vector<std::pair<float, bool>> alignment_params = read_alignment("./config/alignment.dat");
  if (verb)
  {
    std::cout << "============================================================" << std::endl;
    std::cout << "Reading alignment file ./config/alignment.dat" << std::endl;
    std::cout << "Size of alignment_params: " << alignment_params.size() << std::endl;
    // print all alignment parameters
    for (int i = 0; i < alignment_params.size(); i++)
    {
      std::cout << "Alignment parameter " << i << ": " << alignment_params[i].first << " " << alignment_params[i].second << std::endl;
    }
    std::cout << "============================================================" << std::endl;
  }

  int detectors = 0;
  if (newDAQ)
    std::cout << "\nNEW DAQ FILE" << std::endl;

  TFile tempfile(input_files[0].c_str());
  TIter list(tempfile.GetListOfKeys());
  TKey *key;
  while ((key = (TKey *)list()))
  {
    if (!strcmp(key->GetClassName(), "TTree"))
    {
      detectors++;
    }
  }

  detectors = detectors - 1; // board_ids TTree is not included

  tempfile.Close();
  std::cout << "File with " << detectors << " detector(s)" << std::endl;

  // TFile *foutput = new TFile(output_filename + "_board" + std::to_string(board) + ".root", "RECREATE");
  TFile *foutput = new TFile(output_filename + ".root", "RECREATE");
  foutput->cd();

  TDirectory *doutput;
  std::cout << "Creating output directory" << std::endl;

  if (detectors == 1)
  {
    doutput = foutput->mkdir("histos");
    doutput->cd();
    clusterize_detector(0, minADC_h, maxADC_h, minStrip, maxStrip,
                        newDAQ, first_event, NChannels, verb, dynped,
                        invert, maxCN, cntype, NVas, highthreshold, lowthreshold, absolute,
                        symmetric, symmetricwidth,
                        sensor_pitch,
                        version,
                        input_files,
                        nevents,
                        calibration_file,
                        inVA, only_highest);
  }
  else
  {
    for (int i = 0; i < detectors; i++)
    {
      std::cout << "Creating output directory for board " << i << std::endl;
      doutput = foutput->mkdir((TString) "board_" + i);
      doutput->cd();
      clusterize_detector(i, minADC_h, maxADC_h, minStrip, maxStrip,
                          newDAQ, first_event, NChannels, verb, dynped,
                          invert, maxCN, cntype, NVas, highthreshold, lowthreshold, absolute,
                          symmetric, symmetricwidth,
                          sensor_pitch,
                          version,
                          input_files,
                          nevents,
                          calibration_file,
                          inVA, only_highest);
    }
  }

  foutput->Close();
  return 0;
}