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

#include "anyoption.h"
#include "event.h"

AnyOption *opt; // Handle the input options

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

int clusterize_detector(int board, int side, int minADC_h, int maxADC_h, int minStrip, int maxStrip, AnyOption *opt,
                        bool newDAQ, int first_event, int NChannels, bool verb, bool dynped,
                        bool invert, float maxCN, int cntype, int NVas,
                        float highthreshold, float lowthreshold, bool absolute,
                        bool symmetric, int symmetricwidth,
                        int sensor_pitch, bool AMSLO)
{
  //////////////////Histos//////////////////
  TH1F *hADCCluster = // ADC content of all clusters
      new TH1F((TString) "hADCCluster_board_" + board + "_side_" + side, (TString) "hADCCluster_board_" + board + "_side_" + side, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCCluster->GetXaxis()->SetTitle("ADC");

  TH1F *hHighest = // ADC of highest signal
      new TH1F((TString) "hHighest_board_" + board + "_side_" + side, (TString) "hHighest_board_" + board + "_side_" + side, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hHighest->GetXaxis()->SetTitle("ADC");

  TH1F *hADCClusterEdge = // ADC content of all clusters
      new TH1F((TString) "hADCClusterEdge_board_" + board + "_side_" + side, (TString) "hADCClusterEdge_board_" + board + "_side_" + side, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCClusterEdge->GetXaxis()->SetTitle("ADC");

  TH1F *hADCCluster1Strip = // ADC content of clusters with a single strips
      new TH1F((TString) "hADCCluster1Strip_board_" + board + "_side_" + side, (TString) "hADCCluster1Strip_board_" + board + "_side_" + side, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCCluster1Strip->GetXaxis()->SetTitle("ADC");

  TH1F *hADCCluster2Strip = // ADC content of clusters with 2 strips
      new TH1F((TString) "hADCCluster2Strip_board_" + board + "_side_" + side, (TString) "hADCCluster2Strip_board_" + board + "_side_" + side, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCCluster2Strip->GetXaxis()->SetTitle("ADC");

  TH1F *hADCClusterManyStrip = // ADC content of clusters with more than 2 strips
      new TH1F((TString) "hADCClusterManyStrip_board_" + board + "_side_" + side, (TString) "hADCClusterManyStrip_board_" + board + "_side_" + side, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCClusterManyStrip->GetXaxis()->SetTitle("ADC");

  TH1F *hADCClusterSeed = // ADC content of the "seed strip"
      new TH1F((TString) "hADCClusterSeed_board_" + board + "_side_" + side, (TString) "hADCClusterSeed_board_" + board + "_side_" + side, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCClusterSeed->GetXaxis()->SetTitle("ADC");

  TH1F *hPercentageSeed = // percentage of the "seed strip" wrt the whole cluster
      new TH1F((TString) "hPercentageSeed_board_" + board + "_side_" + side, (TString) "hPercentageSeed_board_" + board + "_side_" + side, 200, 20, 150);
  hPercentageSeed->GetXaxis()->SetTitle("percentage");

  TH1F *hPercSeedintegral =
      new TH1F((TString) "hPercSeedintegral_board_" + board + "_side_" + side, (TString) "hPercSeedintegral_board_" + board + "_side_" + side, 200, 20, 150);
  hPercSeedintegral->GetXaxis()->SetTitle("percentage");

  TH1F *hClusterCharge = // sqrt(ADC signal / MIP_ADC) for the cluster
      new TH1F((TString) "hClusterCharge_board_" + board + "_side_" + side, (TString) "hClusterCharge_board_" + board + "_side_" + side, 1000, -0.5, 25.5);
  hClusterCharge->GetXaxis()->SetTitle("Charge");

  TH1F *hSeedCharge = new TH1F((TString) "hSeedCharge_board_" + board + "_side_" + side, (TString) "hSeedCharge_board_" + board + "_side_" + side, 1000, -0.5, 25.5); // sqrt(ADC signal / MIP_ADC) for the seed
  hSeedCharge->GetXaxis()->SetTitle("Charge");

  TH1F *hClusterSN = new TH1F((TString) "hClusterSN_board_" + board + "_side_" + side, (TString) "hClusterSN_board_" + board + "_side_" + side, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h); // cluster S/N
  hClusterSN->GetXaxis()->SetTitle("S/N");

  TH1F *hSeedSN = new TH1F((TString) "hSeedSN_board_" + board + "_side_" + side, (TString) "hSeedSN_board_" + board + "_side_" + side, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h); // seed S/N
  hSeedSN->GetXaxis()->SetTitle("S/N");

  TH1F *hClusterCog = new TH1F((TString) "hClusterCog_board_" + board + "_side_" + side, (TString) "hClusterCog_board_" + board + "_side_" + side, (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5); // clusters center of gravity in terms of strip number
  hClusterCog->GetXaxis()->SetTitle("cog");

  TH1F *hBeamProfile = new TH1F((TString) "hBeamProfile_board_" + board + "_side_" + side, (TString) "hBeamProfile_board_" + board + "_side_" + side, 100, -0.5, 99.5); // clusters center of gravity converted to mm
  hBeamProfile->GetXaxis()->SetTitle("pos (mm)");

  TH1F *hSeedPos = new TH1F((TString) "hSeedPos_board_" + board + "_side_" + side, (TString) "hSeedPos_board_" + board + "_side_" + side, (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5); // clusters seed position in terms of strip number
  hSeedPos->GetXaxis()->SetTitle("strip");

  TH1F *hNclus = new TH1F((TString) "hclus_board_" + board + "_side_" + side, (TString) "hclus_board_" + board + "_side_" + side, 10, -0.5, 9.5); // number of clusters found in each event
  hNclus->GetXaxis()->SetTitle("n clusters");

  TH1F *hNstrip = new TH1F((TString) "hNstrip_board_" + board + "_side_" + side, (TString) "hNstrip_board_" + board + "_side_" + side, 10, -0.5, 9.5); // number of strips per cluster
  hNstrip->GetXaxis()->SetTitle("n strips");

  TH1F *hNstripSeed = new TH1F((TString) "hNstripSeed_board_" + board + "_side_" + side, (TString) "hNstripSeed_board_" + board + "_side_" + side, 10, -0.5, 9.5);
  hNstripSeed->GetXaxis()->SetTitle("n strips over seed threshold");

  TH2F *hADCvsSeed = new TH2F((TString) "hADCvsSeed_board_" + board + "_side_" + side, (TString) "hADCvsSeed_board_" + board + "_side_" + side, 1000, 0, 500, // cluster ADC vs seed ADC
                              1000, 0, 500);
  hADCvsSeed->GetXaxis()->SetTitle("ADC Seed");
  hADCvsSeed->GetYaxis()->SetTitle("ADC Tot");

  TH1F *hEta = new TH1F((TString) "hEta_board_" + board + "_side_" + side, (TString) "hEta_board_" + board + "_side_" + side, 100, 0, 1); // not the real eta function, ignore
  hEta->GetXaxis()->SetTitle("Eta");

  TH1F *hEta1 = new TH1F((TString) "hEta1_board_" + board + "_side_" + side, (TString) "hEta1_board_" + board + "_side_" + side, 100, 0, 1); // not the real eta function, ignore
  hEta1->GetXaxis()->SetTitle("Eta (one seed)");

  TH1F *hEta2 = new TH1F((TString) "hEta2_board_" + board + "_side_" + side, (TString) "hEta2_board_" + board + "_side_" + side, 100, 0, 1); // not the real eta function, ignore
  hEta2->GetXaxis()->SetTitle("Eta (two seed)");

  TH1F *hDifference = new TH1F((TString) "hDifference_board_" + board + "_side_" + side, (TString) "hDifference_board_" + board + "_side_" + side, 200, -5, 5); // relative difference for clusters with 2 strips
  hDifference->GetXaxis()->SetTitle("(ADC_0-ADC_1)/(ADC_0+ADC_1)");

  TH2F *hADCvsWidth = // cluster ADC vs cluster width
      new TH2F((TString) "hADCvsWidth_board_" + board + "_side_" + side, (TString) "hADCvsWidth_board_" + board + "_side_" + side, 10, -0.5, 9.5, 1000, 0, 500);
  hADCvsWidth->GetXaxis()->SetTitle("# of strips");
  hADCvsWidth->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsPos = new TH2F((TString) "hADCvsPos_board_" + board + "_side_" + side, (TString) "hADCvsPos_board_" + board + "_side_" + side, (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5, // cluster ADC vs cog
                             1000, minADC_h, maxADC_h);

  hADCvsPos->GetXaxis()->SetTitle("cog");
  hADCvsPos->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsEta = // ignore
      new TH2F((TString) "hADCvsEta_board_" + board + "_side_" + side, (TString) "hADCvsEta_board_" + board + "_side_" + side, 200, 0, 1, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCvsEta->GetXaxis()->SetTitle("eta");
  hADCvsEta->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsSN = new TH2F((TString) "hADCvsSN_board_" + board + "_side_" + side, (TString) "hADCvsSN_board_" + board + "_side_" + side, 2000, 0, 2500, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hADCvsSN->GetXaxis()->SetTitle("S/N");
  hADCvsSN->GetYaxis()->SetTitle("ADC");

  TH2F *hNStripvsSN =
      new TH2F((TString) "hNstripvsSN_board_" + board + "_side_" + side, (TString) "hNstripvsSN_board_" + board + "_side_" + side, 1000, 0, 2500, 5, -0.5, 4.5);
  hNStripvsSN->GetXaxis()->SetTitle("S/N");
  hNStripvsSN->GetYaxis()->SetTitle("# of strips");

  TH1F *hCommonNoise0 = new TH1F((TString) "hCommonNoise0_board_" + board + "_side_" + side, (TString) "hCommonNoise0_board_" + board + "_side_" + side, 100, -20, 20); // common noise: first algo
  hCommonNoise0->GetXaxis()->SetTitle("CN");

  TH1F *hCommonNoise1 = new TH1F((TString) "hCommonNoise1_board_" + board + "_side_" + side, (TString) "hCommonNoise1_board_" + board + "_side_" + side, 100, -20, 20); // common noise: second algo
  hCommonNoise1->GetXaxis()->SetTitle("CN");

  TH1F *hCommonNoise2 = new TH1F((TString) "hCommonNoise2_board_" + board + "_side_" + side, (TString) "hCommonNoise2_board_" + board + "_side_" + side, 100, -20, 20); // common noise: third algo
  hCommonNoise2->GetXaxis()->SetTitle("CN");

  TH2F *hCommonNoiseVsVA = new TH2F((TString) "hCommonNoiseVsVA_board_" + board + "_side_" + side, (TString) "hCommonNoiseVsVA_board_" + board + "_side_" + side, 100, -20, 20, 10, -0.5, 9.5);
  hCommonNoiseVsVA->GetXaxis()->SetTitle("CN");
  hCommonNoiseVsVA->GetYaxis()->SetTitle("VA");

  TH2F *hEtaVsADC = new TH2F((TString) "hEtaVsADC_board_" + board + "_side_" + side, (TString) "hEtaVsADC_board_" + board + "_side_" + side, 100, 0, 1, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h);
  hCommonNoiseVsVA->GetXaxis()->SetTitle("ADC");
  hCommonNoiseVsVA->GetYaxis()->SetTitle("Eta");

  TH2F *hADC0vsADC1 = new TH2F((TString) "hADC0vsADC1_board_" + board + "_side_" + side, (TString) "hADC0vsADC1_board_" + board + "_side_" + side, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h, (maxADC_h - minADC_h) / 2, minADC_h, maxADC_h); // ADC of first strip vs ADC of second strip for clusters with 2 strips
  hADC0vsADC1->GetXaxis()->SetTitle("ADC0");
  hADC0vsADC1->GetYaxis()->SetTitle("ADC1");

  TGraph *nclus_event = new TGraph(); // number of clusters as a function of event number
  nclus_event->SetName((TString) "nclus_event_board_" + board + "_side_" + side);
  nclus_event->SetTitle((TString) "nclus_event_board_" + board + "_side_" + side);

  // Join ROOTfiles in a single chain
  TChain *chain = new TChain();  // TChain for the first detector TTree (we read 2 detectors with each board on the new DAQ and 1 with the miniTRB)
  TChain *chain2 = new TChain(); // TChain for the second detector TTree

  std::string alphabet = "ABCDEFGHIJKLMNOPQRSTWXYZ";

  if (board == 0) // TTree name depends on DAQ board
  {
    chain->SetName("raw_events"); // simply called raw_events for retrocompatibility with old files from the prototype
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      std::cout << "\nAdding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
      chain->Add(opt->getArgv(ii));
    }
    if (newDAQ)
    {
      chain2->SetName("raw_events_B");
      for (int ii = 0; ii < opt->getArgc(); ii++)
      {
        chain2->Add(opt->getArgv(ii));
      }
      chain->AddFriend(chain2);
    }
  }
  else
  {
    chain->SetName((TString) "raw_events_" + alphabet.at(2 * board));
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      std::cout << "\nAdding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
      chain->Add(opt->getArgv(ii));
    }
    chain2->SetName((TString) "raw_events_" + alphabet.at(2 * board + 1));
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      chain2->Add(opt->getArgv(ii));
    }
    chain->AddFriend(chain2);
  }

  int entries = chain->GetEntries();

  if (opt->getValue("nevents")) // to process only the first "nevents" events in the chain
  {
    unsigned int temp_entries = atoi(opt->getValue("nevents"));
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

  if (board == 0 && side == 0)
  {
    chain->SetBranchAddress("RAW Event", &raw_event, &RAW);
  }
  else
  {
    chain->SetBranchAddress((TString) "RAW Event " + alphabet.at(2 * board + side), &raw_event, &RAW);
  }

  std::vector<cluster> result; // Vector of resulting clusters

  // add t_clusters TTree to output file with name containing board and side
  TString tree_name = "t_clusters_board_" + std::to_string(board) + "_side_" + std::to_string(side);
  TTree *t_clusters = new TTree(tree_name, tree_name);
  t_clusters->Branch("clusters", &result);

  // Read Calibration file
  if (!opt->getValue("calibration"))
  {
    std::cout << "Error: no calibration file" << std::endl;
    return 2;
  }

  calib cal; // calibration struct
  bool is_calib = false;

  is_calib = read_calib(opt->getValue("calibration"), &cal, NChannels, 2 * board + side, verb);

  if (!is_calib)
  {
    std::cout << "ERROR: no calibration file found" << endl;
    return 2;
  }

  // histos for dynamic calibration
  TH1D *hADC[NChannels];
  for (int ch = 0; ch < NChannels; ch++)
  {
    hADC[ch] = new TH1D(Form("pedestal_channel_%d_board_%d_side_%d", ch, board, side), Form("Pedestal %d", ch), 50, 0, -1);
    hADC[ch]->GetXaxis()->SetTitle("ADC");
  }

  // Loop over events
  int perc = 0;   // percentage of processed events
  int maxADC = 0; // max ADC in all the events, to set proper graph/histo limits
  int maxEVT = 0; // event where maxADC was found
  int maxPOS = 0; // position of the strip with value maxADC

  std::cout << "\n===========================================================" << std::endl;
  std::cout << "\nProcessing events for board " << board << " side " << side << std::endl;

  std::cout << "\nProcessing " << entries << " entries, starting from event " << first_event << std::endl;

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

      // if (!AMSLO)
      // {
      //   if (*max_element(signal.begin(), signal.end()) > 4096) // 4096 is the maximum ADC value possible, any more than that means the event is corrupted
      //     continue;
      // }
      // else
      // {
      //   cout << "AMSLO is true" << endl;
      //   sleep(10);
      // }

      if (*max_element(signal.begin(), signal.end()) > maxADC) // searching for the highest ADC value
      {
        maxADC = *max_element(signal.begin(), signal.end());
        maxEVT = index_event;
        std::vector<float>::iterator it = std::find(signal.begin(), signal.end(), maxADC);
        maxPOS = std::distance(signal.begin(), it);
      }

      if (verb)
        std::cout << "Highest strip: " << *max_element(signal.begin(), signal.end()) << std::endl;

      hHighest->Fill(*max_element(signal.begin(), signal.end()));

      result = clusterize_event(&cal, &signal, highthreshold, lowthreshold, // clustering function
                                symmetric, symmetricwidth, absolute, board, side, verb);

      // save result cluster in TTree
      t_clusters->Fill();

      nclus_event->SetPoint(nclus_event->GetN(), index_event, result.size());

      for (int i = 0; i < result.size(); i++)
      {

        if (verb)
        {
          PrintCluster(result.at(i));
        }

        // if (!GoodCluster(result.at(i), &cal))
        //   continue;

        if (result.at(i).address >= minStrip && (result.at(i).address + result.at(i).width - 1) < maxStrip) // cut on position on the detector in terms of strip number
        {
          if (i == 0)
          {
            hNclus->Fill(result.size());
          }

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

  Double_t norm = hADCCluster->GetEntries();
  hADCCluster->Scale(1 / norm);
  hADCCluster->Write();
  delete hADCCluster;

  hHighest->Write();
  delete hHighest;

  norm = hADCClusterEdge->GetEntries();
  hADCClusterEdge->Scale(1 / norm);
  hADCClusterEdge->Write();
  delete hADCClusterEdge;

  norm = hADCCluster1Strip->GetEntries();
  hADCCluster1Strip->Scale(1 / norm);
  hADCCluster1Strip->Write();
  delete hADCCluster1Strip;

  norm = hADCCluster2Strip->GetEntries();
  hADCCluster2Strip->Scale(1 / norm);
  hADCCluster2Strip->Write();
  delete hADCCluster2Strip;

  norm = hADCClusterManyStrip->GetEntries();
  hADCClusterManyStrip->Scale(1 / norm);
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

  nclus_event->SetTitle((TString) "nClus vs nEvent_board_" + board + "_side_" + side);
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

  float highthreshold = 3.5;
  float lowthreshold = 1.0;
  int symmetricwidth = 0;
  int cntype = 0;
  int maxCN = 999;

  int NChannels = 384;
  int NVas = 6;
  int minStrip = 0;
  int maxStrip = 383;
  int minADC_h = 0;
  int maxADC_h = 500;
  float sensor_pitch = 0.150;

  bool newDAQ = false;
  int side = 0;
  int board = 0;

  opt = new AnyOption();
  opt->addUsage("Usage: ./raw_clusterize [options] [arguments] rootfile1 rootfile2 ...");
  opt->addUsage("");
  opt->addUsage("Options: ");
  opt->addUsage("  -h, --help       ................................. Print this help ");
  opt->addUsage("  -v, --verbose    ................................. Verbose ");
  opt->addUsage("  --nevents        ................................. Number of events to process ");
  opt->addUsage("  --first          ................................. First event to process ");
  opt->addUsage("  --version        ................................. 1212 for 6VA  miniTRB");
  opt->addUsage("                   ................................. 1313 for 10VA miniTRB");
  opt->addUsage("                   ................................. 2020 for FOOT DAQ");
  opt->addUsage("                   ................................. 2021 for PAN StripX");
  opt->addUsage("                   ................................. 2022 for PAN StripY");
  opt->addUsage("                   ................................. 2023 for AMSL0");
  opt->addUsage("  --output         ................................. Output ROOT file ");
  opt->addUsage("  --calibration    ................................. Calibration file ");
  opt->addUsage("  --dynped         ................................. Enable dynamic pedestals ");
  opt->addUsage("  --highthreshold  ................................. High threshold used in the clusterization ");
  opt->addUsage("  --lowthreshold   ................................. Low threshold used in the clusterization ");
  opt->addUsage("  -s, --symmetric  ................................. Use symmetric cluster instead of double threshold ");
  opt->addUsage("  --symmetricwidth ................................. Width of symmetric clusters ");
  opt->addUsage("  -a, --absolute   ................................. Use absolute ADC value instead of S/N for thresholds ");
  opt->addUsage("  --cn             ................................. CN algorithm selection (0,1,2) ");
  opt->addUsage("  --maxcn          ................................. Max CN for a good event");
  opt->addUsage("  --minstrip       ................................. Minimun strip number to analyze");
  opt->addUsage("  --maxstrip       ................................. Maximum strip number to analyze");
  opt->addUsage("  --min_histo_ADC  ................................. Minimun ADC value on histo axis");
  opt->addUsage("  --max_histo_ADC  ................................. Maximum ADC value on histo axis");
  opt->addUsage("  --invert         ................................. To search for negative signal peaks (prototype ADC board)");

  opt->setFlag("help", 'h');
  opt->setFlag("symmetric", 's');
  opt->setFlag("absolute", 'a');
  opt->setFlag("verbose", 'v');
  opt->setFlag("invert");
  opt->setFlag("dynped");

  opt->setOption("version");
  opt->setOption("nevents");
  opt->setOption("first");
  opt->setOption("output");
  opt->setOption("calibration");
  opt->setOption("highthreshold");
  opt->setOption("lowthreshold");
  opt->setOption("symmetricwidth");
  opt->setOption("cn");
  opt->setOption("maxcn");
  opt->setOption("minstrip");
  opt->setOption("maxstrip");
  opt->setOption("min_histo_ADC");
  opt->setOption("max_histo_ADC");

  opt->processFile("./options.txt");
  opt->processCommandArgs(argc, argv);

  if (!opt->hasOptions())
  { /* print usage if no options */
    opt->printUsage();
    delete opt;
    return 2;
  }

  if (!opt->getValue("version"))
  {
    std::cout << "ERROR: no DAQ board version provided" << std::endl;
    return 2;
  }

  if (atoi(opt->getValue("version")) == 1212) // original DaMPE miniTRB system
  {
    NChannels = 384;
    NVas = 6;
    minStrip = 0;
    maxStrip = 383;
    sensor_pitch = 0.242;
  }
  else if (atoi(opt->getValue("version")) == 1313) // modded DaMPE miniTRB system for the first FOOT prototype
  {
    NChannels = 640;
    NVas = 10;
    minStrip = 0;
    maxStrip = 639;
    sensor_pitch = 0.150;
  }
  else if (atoi(opt->getValue("version")) == 2020) // FOOT ADC boards + DE10Nano
  {
    NChannels = 640;
    NVas = 10;
    minStrip = 0;
    maxStrip = 639;
    sensor_pitch = 0.150;
    newDAQ = true;
  }
  else if (atoi(opt->getValue("version")) == 2021) // PAN StripX
  {
    NChannels = 2048;
    NVas = 32;
    minStrip = 0;
    maxStrip = 2047;
    sensor_pitch = 0.050;
  }
  else if (atoi(opt->getValue("version")) == 2022) // PAN StripY
  {
    NChannels = 128;
    NVas = 1;
    minStrip = 0;
    maxStrip = 127;
    sensor_pitch = 0.400;
  }
  else if (atoi(opt->getValue("version")) == 2023) // AMSL0
  {
    NChannels = 1024;
    NVas = 16;
    minStrip = 0;
    maxStrip = 1023;
    sensor_pitch = 0.109;
    maxADC_h = 30000;
  }
  else
  {
    std::cout << "ERROR: invalid DAQ board version" << std::endl;
    return 2;
  }

  if (opt->getFlag("help") || opt->getFlag('h'))
    opt->printUsage();

  if (opt->getFlag("verbose") || opt->getFlag('v'))
    verb = true;

  if (opt->getFlag("invert"))
    invert = true;

  if (opt->getValue("highthreshold"))
    highthreshold = atof(opt->getValue("highthreshold"));

  if (opt->getValue("lowthreshold"))
    lowthreshold = atof(opt->getValue("lowthreshold"));

  if (opt->getValue("symmetric"))
    symmetric = true;

  if (opt->getValue("dynped"))
    dynped = true;

  if (opt->getValue("symmetric") && opt->getValue("symmetricwidth"))
    symmetricwidth = atoi(opt->getValue("symmetricwidth"));

  if (opt->getValue("absolute"))
    absolute = true;

  if (opt->getValue("cn"))
    cntype = atoi(opt->getValue("cn"));

  if (opt->getValue("maxcn"))
    maxCN = atoi(opt->getValue("maxcn"));

  if (opt->getValue("minstrip"))
    minStrip = atoi(opt->getValue("minstrip"));

  if (opt->getValue("maxstrip"))
    maxStrip = atoi(opt->getValue("maxstrip"));

  if (opt->getValue("min_ADC_histo"))
    minADC_h = atoi(opt->getValue("min_histo_ADC"));

  if (opt->getValue("max_histo_ADC"))
    maxADC_h = atoi(opt->getValue("max_histo_ADC"));

  int first_event = 0;
  if (opt->getValue("first")) // to choose the first event to process
  {
    first_event = atoi(opt->getValue("first"));
  }

  // Create output ROOTfile
  TString output_filename;
  if (opt->getValue("output"))
  {
    output_filename = opt->getValue("output");
  }
  else
  {
    std::cout << "Error: no output file" << std::endl;
    return 2;
  }

  std::vector<std::pair<float, bool>> alignment_params = read_alignment("./config/alignment.dat");
  if (verb)
  {
    std::cout << "============================================================" << std::endl;
    std::cout << "Reading alignment file ./config/alignment.dat" << std::endl;
    cout << "Size of alignment_params: " << alignment_params.size() << endl;
    // print all alignment parameters
    for (int i = 0; i < alignment_params.size(); i++)
    {
      cout << "Alignment parameter " << i << ": " << alignment_params[i].first << " " << alignment_params[i].second << endl;
    }
    std::cout << "============================================================" << std::endl;
  }

  int detectors = 0;
  if (newDAQ)
    std::cout << "\nNEW DAQ FILE" << std::endl;

  TFile tempfile(opt->getArgv(0));
  TIter list(tempfile.GetListOfKeys());
  TKey *key;
  while ((key = (TKey *)list()))
  {
    if (!strcmp(key->GetClassName(), "TTree"))
    {
      detectors++;
    }
  }
  tempfile.Close();
  std::cout << "File with " << detectors << " detector(s)" << std::endl;

  // TFile *foutput = new TFile(output_filename + "_board" + std::to_string(board) + "_side" + std::to_string(side) + ".root", "RECREATE");
  TFile *foutput = new TFile(output_filename + ".root", "RECREATE");
  foutput->cd();

  TDirectory *doutput;
  cout << "Creating output directory" << endl;

  if (detectors == 1)
  {
    doutput = foutput->mkdir("detector");
    doutput->cd();
    clusterize_detector(0, 0, minADC_h, maxADC_h, minStrip, maxStrip, opt,
                        newDAQ, first_event, NChannels, verb, dynped,
                        invert, maxCN, cntype, NVas, highthreshold, lowthreshold, absolute,
                        symmetric, symmetricwidth,
                        sensor_pitch,
                        atoi(opt->getValue("version")) == 2023);
  }
  else
  {
    for (int i = 0; i < detectors / 2; i++)
    {
      cout << "Creating output directory " << i << endl;
      doutput = foutput->mkdir((TString) "board_" + i + "_side_0");
      doutput->cd();
      clusterize_detector(i, 0, minADC_h, maxADC_h, minStrip, maxStrip, opt,
                          newDAQ, first_event, NChannels, verb, dynped,
                          invert, maxCN, cntype, NVas, highthreshold, lowthreshold, absolute,
                          symmetric, symmetricwidth,
                          sensor_pitch,
                          atoi(opt->getValue("version")) == 2023);

      doutput = foutput->mkdir((TString) "board_" + i + "_side_1");
      doutput->cd();
      clusterize_detector(i, 1, minADC_h, maxADC_h, minStrip, maxStrip, opt,
                          newDAQ, first_event, NChannels, verb, dynped,
                          invert, maxCN, cntype, NVas, highthreshold, lowthreshold, absolute,
                          symmetric, symmetricwidth,
                          sensor_pitch,
                          atoi(opt->getValue("version")) == 2023);
    }
  }

  foutput->Close();
  return 0;
}
