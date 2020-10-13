#include "TChain.h"
#include "TFile.h"
#include "TH1.h"
#include "TH2.h"
#include "TTree.h"
#include <iostream>
#include "environment.h"
#include <algorithm>

#include "anyoption.h"
#include "miniTRB.h"

#if OMP_ == 1
#include "omp.h"
#endif

AnyOption *opt; //Handle the option input

int main(int argc, char *argv[])
{
  bool symmetric = false;
  bool absolute = false;
  bool verb = false;

  float highthreshold = 3.5;
  float lowthreshold = 1.0;
  int symmetricwidth = 0;
  int cntype = 0;
  int maxCN = 999;

  int NChannels = 384;
  int NVas = 6;
  int minStrip = 0;
  int maxStrip = 383;
  float sensor_pitch = 0.150;

  opt = new AnyOption();
  opt->addUsage("Usage: ./miniTRB_clusterize [options] [arguments] rootfile1 rootfile2 ...");
  opt->addUsage("");
  opt->addUsage("Options: ");
  opt->addUsage("  -h, --help       ................................. Print this help ");
  opt->addUsage("  -v, --verbose    ................................. Verbose ");
  opt->addUsage("  --nevents    ................................. Number of events to process ");
  opt->addUsage("  --version        ................................. 1212 for 6VA or 1313 for 10VA miniTRB ");
  opt->addUsage("  --output         ................................. Output ROOT file ");
  opt->addUsage("  --calibration    ................................. Calibration file ");
  opt->addUsage("  --highthreshold  ................................. High threshold used in the clusterization ");
  opt->addUsage("  --lowthreshold   ................................. Low threshold used in the clusterization ");
  opt->addUsage("  -s, --symmetric  ................................. Use symmetric cluster instead of double threshold ");
  opt->addUsage("  --symmetricwidth ................................. Width of symmetric clusters ");
  opt->addUsage("  -a, --absolute   ................................. Use absolute ADC value instead of S/N for thresholds ");
  opt->addUsage("  --cn             ................................. CN algorithm selection (0,1,2) ");
  opt->addUsage("  --maxcn          ................................. Max CN for a good event");
  opt->addUsage("  --minstrip       ................................. Minimun strip number to analyze");
  opt->addUsage("  --maxstrip       ................................. Maximum strip number to analyze");

  opt->setFlag("help", 'h');
  opt->setFlag("symmetric", 's');
  opt->setFlag("absolute", 'a');
  opt->setFlag("verbose", 'v');

  opt->setOption("version");
  opt->setOption("nevents");
  opt->setOption("output");
  opt->setOption("calibration");
  opt->setOption("highthreshold");
  opt->setOption("lowthreshold");
  opt->setOption("symmetricwidth");
  opt->setOption("cn");
  opt->setOption("maxcn");
  opt->setOption("minstrip");
  opt->setOption("maxstrip");

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
    std::cout << "ERROR: no miniTRB version provided" << std::endl;
    return 2;
  }

  if (atoi(opt->getValue("version")) == 1212)
  {
    NChannels = 384;
    NVas = 6;
    minStrip = 0;
    maxStrip = 383;
    sensor_pitch = 0.242;
  }
  else if (atoi(opt->getValue("version")) == 1313)
  {
    NChannels = 640;
    NVas = 10;
    minStrip = 0;
    maxStrip = 639;
    sensor_pitch = 0.150;
  }
  else
  {
    std::cout << "ERROR: invalid miniTRB version" << std::endl;
    return 2;
  }

  if (opt->getFlag("help") || opt->getFlag('h'))
    opt->printUsage();

  if (opt->getFlag("verbose") || opt->getFlag('v'))
    verb = true;

  if (opt->getValue("highthreshold"))
    highthreshold = atof(opt->getValue("highthreshold"));

  if (opt->getValue("lowthreshold"))
    lowthreshold = atof(opt->getValue("lowthreshold"));

  if (opt->getValue("symmetric"))
    symmetric = true;

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

  //////////////////Histos//////////////////
  TH1F *hADCCluster =
      new TH1F("hADCCluster", "hADCCluster", 100, 0, 100);
  hADCCluster->GetXaxis()->SetTitle("ADC");

  TH1F *hADCCluster1Strip =
      new TH1F("hADCCluster1Strip", "hADCCluster1Strip", 100, 0, 100);
  hADCCluster1Strip->GetXaxis()->SetTitle("ADC");

  TH1F *hADCCluster2Strip =
      new TH1F("hADCCluster2Strip", "hADCCluster2Strip", 100, 0, 200);
  hADCCluster2Strip->GetXaxis()->SetTitle("ADC");

  TH1F *hADCClusterManyStrip = new TH1F(
      "hADCClusterManyStrip", "hADCClusterManyStrip", 100, 0, 200);
  hADCClusterManyStrip->GetXaxis()->SetTitle("ADC");

  TH1F *hADCClusterSeed =
      new TH1F("hADCClusterSeed", "hADCClusterSeed", 100, 0, 200);
  hADCClusterSeed->GetXaxis()->SetTitle("ADC");

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

  TH1F *hClusterSN = new TH1F("hClusterSN", "hClusterSN", 200, 0, 250);
  hClusterSN->GetXaxis()->SetTitle("S/N");

  TH1F *hSeedSN = new TH1F("hSeedSN", "hSeedSN", 1000, 0, 2000);
  hSeedSN->GetXaxis()->SetTitle("S/N");

  TH1F *hClusterCog = new TH1F("hClusterCog", "hClusterCog", (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5);
  hClusterCog->GetXaxis()->SetTitle("cog");

  TH1F *hBeamProfile = new TH1F("hBeamProfile", "hBeamProfile", 100, -0.5, 99.5);
  hBeamProfile->GetXaxis()->SetTitle("pos (mm)");

  TH1F *hSeedPos = new TH1F("hSeedPos", "hSeedPos", (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5);
  hSeedPos->GetXaxis()->SetTitle("strip");

  TH1F *hNclus = new TH1F("hclus", "hclus", 10, -0.5, 9.5);
  hNclus->GetXaxis()->SetTitle("n clusters");

  TH1F *hNstrip = new TH1F("hNstrip", "hNstrip", 10, -0.5, 9.5);
  hNstrip->GetXaxis()->SetTitle("n strips");

  TH1F *hNstripSeed = new TH1F("hNstripSeed", "hNstripSeed", 10, -0.5, 9.5);
  hNstripSeed->GetXaxis()->SetTitle("n strips over seed threshold");

  TH2F *hADCvsSeed = new TH2F("hADCvsSeed", "hADCvsSeed", 200, 0, 100,
                              200, 0, 100);
  hADCvsSeed->GetXaxis()->SetTitle("ADC Seed");
  hADCvsSeed->GetYaxis()->SetTitle("ADC Tot");

  TH1F *hEta = new TH1F("hEta", "hEta", 100, 0, 1);
  hEta->GetXaxis()->SetTitle("Eta");

  TH1F *hEta1 = new TH1F("hEta1", "hEta1", 100, 0, 1);
  hEta1->GetXaxis()->SetTitle("Eta (one seed)");

  TH1F *hEta2 = new TH1F("hEta2", "hEta2", 100, 0, 1);
  hEta2->GetXaxis()->SetTitle("Eta (two seed)");

  TH1F *hDifference = new TH1F("hDifference", "hDifference", 200, -5, 5);
  hDifference->GetXaxis()->SetTitle("(ADC_0-ADC_1)/(ADC_0+ADC_1)");

  TH2F *hADCvsWidth =
      new TH2F("hADCvsWidth", "hADCvsWidth", 10, -0.5, 9.5, 100, 0, 200);
  hADCvsWidth->GetXaxis()->SetTitle("# of strips");
  hADCvsWidth->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsPos = new TH2F("hADCvsPos", "hADCvsPos", (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5,
                             10000, 0, 2000);
  hADCvsPos->GetXaxis()->SetTitle("cog");
  hADCvsPos->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsEta =
      new TH2F("hADCvsEta", "hADCvsEta", 200, 0, 1, 100, 0, 200);
  hADCvsEta->GetXaxis()->SetTitle("eta");
  hADCvsEta->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsSN = new TH2F("hADCvsSN", "hADCvsSN", 200, 0, 250, 100, 0, 200);
  hADCvsSN->GetXaxis()->SetTitle("S/N");
  hADCvsSN->GetYaxis()->SetTitle("ADC");

  TH2F *hNStripvsSN =
      new TH2F("hNstripvsSN", "hNstripvsSN", 1000, 0, 500, 5, -0.5, 4.5);
  hNStripvsSN->GetXaxis()->SetTitle("S/N");
  hNStripvsSN->GetYaxis()->SetTitle("# of strips");

  TH1F *hCommonNoise0 = new TH1F("hCommonNoise0", "hCommonNoise0", 100, -20, 20);
  hCommonNoise0->GetXaxis()->SetTitle("CN");

  TH1F *hCommonNoise1 = new TH1F("hCommonNoise1", "hCommonNoise1", 100, -20, 20);
  hCommonNoise1->GetXaxis()->SetTitle("CN");

  TH1F *hCommonNoise2 = new TH1F("hCommonNoise2", "hCommonNoise2", 100, -20, 20);
  hCommonNoise2->GetXaxis()->SetTitle("CN");

  TH2F *hCommonNoiseVsVA = new TH2F("hCommonNoiseVsVA", "hCommonNoiseVsVA", 100, -20, 20, 10, -0.5, 9.5);
  hCommonNoiseVsVA->GetXaxis()->SetTitle("CN");
  hCommonNoiseVsVA->GetYaxis()->SetTitle("VA");

  TH2F *hADC0vsADC1 = new TH2F("hADC0vsADC1", "hADC0vsADC1", 100, 0, 50, 100, 0, 50);
  hADC0vsADC1->GetXaxis()->SetTitle("ADC0");
  hADC0vsADC1->GetYaxis()->SetTitle("ADC1");

  // Join ROOTfiles in a single chain
  TChain *chain = new TChain("raw_events"); //Chain input rootfiles
  for (int ii = 0; ii < opt->getArgc(); ii++)
  {
    std::cout << "Adding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
    chain->Add(opt->getArgv(ii));
  }

  int entries = chain->GetEntries();
  std::cout << "This run has " << entries << " entries" << std::endl;

  if (opt->getValue("nevents"))
  {
    unsigned int temp_entries = atoi(opt->getValue("nevents"));
    if (temp_entries < entries)
    {
      entries = temp_entries;
    }
  }
  std::cout << "Processing " << entries << " entries" << std::endl;

  // Read raw event from input chain TTree
  std::vector<unsigned short> *raw_event = 0;
  TBranch *RAW = 0;
  chain->SetBranchAddress("RAW Event", &raw_event, &RAW);

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

  TFile *foutput = new TFile(output_filename.Data(), "RECREATE");
  foutput->cd();

  //Read Calibration file
  if (!opt->getValue("calibration"))
  {
    std::cout << "Error: no calibration file" << std::endl;
    return 2;
  }

  calib cal;
  read_calib(opt->getValue("calibration"), &cal);

  // Loop over events
  int perc = 0;
  int maxADC = 0;
  int maxEVT = 0;
  int maxPOS = 0;

  for (int index_event = 0; index_event < entries; index_event++)
  {
    chain->GetEntry(index_event);

    if (verb)
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
          if (cal.status[i] != 0)
          {
            signal.at(i) = 0;
          }
          else
          {
            signal.at(i) = (raw_event->at(i) - cal.ped[i]);
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

    for (int va = 0; va < NVas; va++) //Loop on VA
    {
      float cn = GetCN(&signal, va, 0);
      if (cn != -999 && abs(cn) < maxCN)
      {
        hCommonNoise0->Fill(cn);
        //hCommonNoiseVsVA->Fill(cn, va);
      }
    }

    for (int va = 0; va < NVas; va++) //Loop on VA
    {
      float cn = GetCN(&signal, va, 1);
      if (cn != -999 && abs(cn) < maxCN)
      {
        hCommonNoise1->Fill(cn);
        //hCommonNoiseVsVA->Fill(cn, va);
      }
    }

    for (int va = 0; va < NVas; va++) //Loop on VA
    {
      float cn = GetCN(&signal, va, 2);
      if (cn != -999 && abs(cn) < maxCN)
      {
        hCommonNoise2->Fill(cn);
        //hCommonNoiseVsVA->Fill(cn, va);
      }
    }

    bool goodCN = true;

    if (cntype >= 0)
    {
#pragma omp parallel for                //Multithread for loop
      for (int va = 0; va < NVas; va++) //Loop on VA
      {
        float cn = GetCN(&signal, va, cntype);
        if (cn != -999 && abs(cn) < maxCN)
        {
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
            goodCN = false;
          }
        }
      }
    }

    try
    {
      if (!goodCN)
        continue;

      if (*max_element(signal.begin(), signal.end()) > 4096) //4096 is the maximum ADC value possible, any more than that means the event is corrupted
        continue;

      if (*max_element(signal.begin(), signal.end()) > maxADC)
      {
        maxADC = *max_element(signal.begin(), signal.end());
        maxEVT = index_event;
        std::vector<float>::iterator it = std::find(signal.begin(), signal.end(), maxADC);
        maxPOS = std::distance(signal.begin(), it);
      }

      result = clusterize(&cal, &signal, highthreshold, lowthreshold,
                          symmetric, symmetricwidth, absolute);

      for (int i = 0; i < result.size(); i++)
      {
        if (verb)
        {
          PrintCluster(result.at(i));
        }

        if (!GoodCluster(result.at(i), &cal))
          continue;

        //if (!(GetClusterCOG(result.at(i)) > 205 && GetClusterCOG(result.at(i)) < 207)) continue;
        //  PrintCluster(result.at(i));

        if (result.at(i).address >= minStrip && (result.at(i).address + result.at(i).width - 1) < maxStrip)
        {
          if (i == 0)
          {
            hNclus->Fill(result.size());
          }

          hADCCluster->Fill(GetClusterSignal(result.at(i)));

          if (result.at(i).width == 1)
          {
            hADCCluster1Strip->Fill(GetClusterSignal(result.at(i)));
          }
          else if (result.at(i).width == 2)
          {
            hADCCluster2Strip->Fill(GetClusterSignal(result.at(i)));
          }
          else
          {
            hADCClusterManyStrip->Fill(GetClusterSignal(result.at(i)));
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
          if (result.at(i).width == 2)
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

  Double_t norm = hADCCluster->GetEntries();
  //hADCCluster->Scale(1 / norm);
  hADCCluster->Write();

  Double_t norm1 = hADCCluster1Strip->GetEntries();
  //hADCCluster1Strip->Scale(1 / norm1);
  hADCCluster1Strip->Write();

  Double_t norm2 = hADCCluster2Strip->GetEntries();
  //hADCCluster2Strip->Scale(1 / norm2);
  hADCCluster2Strip->Write();

  Double_t norm3 = hADCClusterManyStrip->GetEntries();
  //hADCClusterManyStrip->Scale(1 / norm3);
  hADCClusterManyStrip->Write();

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

  foutput->Close();
  return 0;
}
