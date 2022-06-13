#include "TROOT.h"
#include "TSystem.h"
#include "TChain.h"
#include "TFile.h"
#include "TF1.h"
#include "TH1.h"
#include "TH2.h"
#include "TGraph.h"
#include "TTree.h"
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

int main(int argc, char *argv[])
{
  //generating shared library for cluster saving
  TString command = TString(".L ") + gSystem->pwd() + TString("/src/types.C+");
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
  opt->addUsage("  --board          ................................. ADC board to analyze (0, 1, 2)");
  opt->addUsage("  --side           ................................. Sensor side for new FOOT DAQ (0, 1)");
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
  opt->setOption("side");
  opt->setOption("board");

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

  if (opt->getValue("side"))
    side = atoi(opt->getValue("side"));

  if (opt->getValue("board"))
    board = atoi(opt->getValue("board"));

  //////////////////Histos//////////////////

  TH1F *hADCCluster = // ADC content of all clusters
      new TH1F("hADCCluster", "hADCCluster", 2500, 0, 5000);
  hADCCluster->GetXaxis()->SetTitle("ADC");

  TH1F *hHighest = // ADC of highest signal
      new TH1F("hHighest", "hHighest", 2500, 0, 5000);
  hHighest->GetXaxis()->SetTitle("ADC");

  TH1F *hADCClusterEdge = // ADC content of all clusters
      new TH1F("hADCClusterEdge", "hADCClusterEdge", 2500, 0, 5000);
  hADCClusterEdge->GetXaxis()->SetTitle("ADC");

  TH1F *hADCCluster1Strip = // ADC content of clusters with a single strips
      new TH1F("hADCCluster1Strip", "hADCCluster1Strip", 2500, 0, 5000);
  hADCCluster1Strip->GetXaxis()->SetTitle("ADC");

  TH1F *hADCCluster2Strip = // ADC content of clusters with 2 strips
      new TH1F("hADCCluster2Strip", "hADCCluster2Strip", 2500, 0, 5000);
  hADCCluster2Strip->GetXaxis()->SetTitle("ADC");

  TH1F *hADCClusterManyStrip = new TH1F( // ADC content of clusters with more than 2 strips
      "hADCClusterManyStrip", "hADCClusterManyStrip", 2500, 0, 5000);
  hADCClusterManyStrip->GetXaxis()->SetTitle("ADC");

  TH1F *hADCClusterSeed = // ADC content of the "seed strip"
      new TH1F("hADCClusterSeed", "hADCClusterSeed", 2500, 0, 5000);
  hADCClusterSeed->GetXaxis()->SetTitle("ADC");

  TH1F *hPercentageSeed = // percentage of the "seed strip" wrt the whole cluster
      new TH1F("hPercentageSeed", "hPercentageSeed", 200, 20, 150);
  hPercentageSeed->GetXaxis()->SetTitle("percentage");

  TH1F *hPercSeedintegral =
      new TH1F("hPercSeedintegral", "hPercSeedintegral", 200, 20, 150);
  hPercSeedintegral->GetXaxis()->SetTitle("percentage");

  TH1F *hClusterCharge = // sqrt(ADC signal / MIP_ADC) for the cluster
      new TH1F("hClusterCharge", "hClusterCharge", 1000, -0.5, 25.5);
  hClusterCharge->GetXaxis()->SetTitle("Charge");

  TH1F *hSeedCharge = new TH1F("hSeedCharge", "hSeedCharge", 1000, -0.5, 25.5); // sqrt(ADC signal / MIP_ADC) for the seed
  hSeedCharge->GetXaxis()->SetTitle("Charge");

  TH1F *hClusterSN = new TH1F("hClusterSN", "hClusterSN", 2500, 0, 5000); // cluster S/N
  hClusterSN->GetXaxis()->SetTitle("S/N");

  TH1F *hSeedSN = new TH1F("hSeedSN", "hSeedSN", 2000, 0, 5000); // seed S/N
  hSeedSN->GetXaxis()->SetTitle("S/N");

  TH1F *hClusterCog = new TH1F("hClusterCog", "hClusterCog", (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5); // clusters center of gravity in terms of strip number
  hClusterCog->GetXaxis()->SetTitle("cog");

  TH1F *hBeamProfile = new TH1F("hBeamProfile", "hBeamProfile", 100, -0.5, 99.5); // clusters center of gravity converted to mm
  hBeamProfile->GetXaxis()->SetTitle("pos (mm)");

  TH1F *hSeedPos = new TH1F("hSeedPos", "hSeedPos", (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5); // clusters seed position in terms of strip number
  hSeedPos->GetXaxis()->SetTitle("strip");

  TH1F *hNclus = new TH1F("hclus", "hclus", 10, -0.5, 9.5); // number of clusters found in each event
  hNclus->GetXaxis()->SetTitle("n clusters");

  TH1F *hNstrip = new TH1F("hNstrip", "hNstrip", 10, -0.5, 9.5); // number of strips per cluster
  hNstrip->GetXaxis()->SetTitle("n strips");

  TH1F *hNstripSeed = new TH1F("hNstripSeed", "hNstripSeed", 10, -0.5, 9.5);
  hNstripSeed->GetXaxis()->SetTitle("n strips over seed threshold");

  TH2F *hADCvsSeed = new TH2F("hADCvsSeed", "hADCvsSeed", 2500, 0, 5000, // cluster ADC vs seed ADC
                              2500, 0, 5000);
  hADCvsSeed->GetXaxis()->SetTitle("ADC Seed");
  hADCvsSeed->GetYaxis()->SetTitle("ADC Tot");

  TH1F *hEta = new TH1F("hEta", "hEta", 100, 0, 1); // not the real eta function, ignore
  hEta->GetXaxis()->SetTitle("Eta");

  TH1F *hEta1 = new TH1F("hEta1", "hEta1", 100, 0, 1); // not the real eta function, ignore
  hEta1->GetXaxis()->SetTitle("Eta (one seed)");

  TH1F *hEta2 = new TH1F("hEta2", "hEta2", 100, 0, 1); // not the real eta function, ignore
  hEta2->GetXaxis()->SetTitle("Eta (two seed)");

  TH1F *hDifference = new TH1F("hDifference", "hDifference", 200, -5, 5); // relative difference for clusters with 2 strips
  hDifference->GetXaxis()->SetTitle("(ADC_0-ADC_1)/(ADC_0+ADC_1)");

  TH2F *hADCvsWidth = // cluster ADC vs cluster width
      new TH2F("hADCvsWidth", "hADCvsWidth", 10, -0.5, 9.5, 2500, 0, 5000);
  hADCvsWidth->GetXaxis()->SetTitle("# of strips");
  hADCvsWidth->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsPos = new TH2F("hADCvsPos", "hADCvsPos", (maxStrip - minStrip), minStrip - 0.5, maxStrip - 0.5, // cluster ADC vs cog
                             2500, 0, 5000);
  hADCvsPos->GetXaxis()->SetTitle("cog");
  hADCvsPos->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsEta = // ignore
      new TH2F("hADCvsEta", "hADCvsEta", 200, 0, 1, 2000, 0, 5000);
  hADCvsEta->GetXaxis()->SetTitle("eta");
  hADCvsEta->GetYaxis()->SetTitle("ADC");

  TH2F *hADCvsSN = new TH2F("hADCvsSN", "hADCvsSN", 2000, 0, 2500, 2000, 0, 5000);
  hADCvsSN->GetXaxis()->SetTitle("S/N");
  hADCvsSN->GetYaxis()->SetTitle("ADC");

  TH2F *hNStripvsSN =
      new TH2F("hNstripvsSN", "hNstripvsSN", 1000, 0, 2500, 5, -0.5, 4.5);
  hNStripvsSN->GetXaxis()->SetTitle("S/N");
  hNStripvsSN->GetYaxis()->SetTitle("# of strips");

  TH1F *hCommonNoise0 = new TH1F("hCommonNoise0", "hCommonNoise0", 100, -20, 20); // common noise: first algo
  hCommonNoise0->GetXaxis()->SetTitle("CN");

  TH1F *hCommonNoise1 = new TH1F("hCommonNoise1", "hCommonNoise1", 100, -20, 20); // common noise: second algo
  hCommonNoise1->GetXaxis()->SetTitle("CN");

  TH1F *hCommonNoise2 = new TH1F("hCommonNoise2", "hCommonNoise2", 100, -20, 20); // common noise: third algo
  hCommonNoise2->GetXaxis()->SetTitle("CN");

  TH2F *hCommonNoiseVsVA = new TH2F("hCommonNoiseVsVA", "hCommonNoiseVsVA", 100, -20, 20, 10, -0.5, 9.5);
  hCommonNoiseVsVA->GetXaxis()->SetTitle("CN");
  hCommonNoiseVsVA->GetYaxis()->SetTitle("VA");

  TH2F *hEtaVsADC = new TH2F("hEtaVsADC", "hEtaVsADC", 100, 0, 1, 1000, 0, 250);
  hCommonNoiseVsVA->GetXaxis()->SetTitle("ADC");
  hCommonNoiseVsVA->GetYaxis()->SetTitle("Eta");

  TH2F *hADC0vsADC1 = new TH2F("hADC0vsADC1", "hADC0vsADC1", 2500, 0, 5000, 2500, 0, 5000); // ADc of first strip vs ADC of second strip for clusters with 2 strips
  hADC0vsADC1->GetXaxis()->SetTitle("ADC0");
  hADC0vsADC1->GetYaxis()->SetTitle("ADC1");

  TGraph *nclus_event = new TGraph(); // number of clusters as a function of event number

  // Join ROOTfiles in a single chain
  TChain *chain = new TChain();  // TChain for the first detector TTree (we read 2 detectors with each board on the new DAQ and 1 with the miniTRB)
  TChain *chain2 = new TChain(); // TChain for the second detectoe TTree

  if (board == 0) // TTree name depends on DAQ board
  {
    chain->SetName("raw_events"); // simply called raw_events for retrocompatibility with old files from the prototype
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      std::cout << "Adding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
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
  else if (board == 1)
  {
    chain->SetName("raw_events_C");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      std::cout << "Adding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
      chain->Add(opt->getArgv(ii));
    }
    chain2->SetName("raw_events_D");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      chain2->Add(opt->getArgv(ii));
    }
    chain->AddFriend(chain2);
  }
  else if (board == 2)
  {
    chain->SetName("raw_events_E");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      std::cout << "Adding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
      chain->Add(opt->getArgv(ii));
    }
    chain2->SetName("raw_events_F");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      chain2->Add(opt->getArgv(ii));
    }
    chain->AddFriend(chain2);
  }
  else if (board == 3)
  {
    chain->SetName("raw_events_G");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      std::cout << "Adding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
      chain->Add(opt->getArgv(ii));
    }
    chain2->SetName("raw_events_H");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      chain2->Add(opt->getArgv(ii));
    }
    chain->AddFriend(chain2);
  }
  else if (board == 4)
  {
    chain->SetName("raw_events_I");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      std::cout << "Adding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
      chain->Add(opt->getArgv(ii));
    }
    chain2->SetName("raw_events_J");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      chain2->Add(opt->getArgv(ii));
    }
    chain->AddFriend(chain2);
  }
  else if (board == 5)
  {
    chain->SetName("raw_events_K");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      std::cout << "Adding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
      chain->Add(opt->getArgv(ii));
    }
    chain2->SetName("raw_events_L");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      chain2->Add(opt->getArgv(ii));
    }
    chain->AddFriend(chain2);
  }
  else if (board == 6)
  {
    chain->SetName("raw_events_M");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      std::cout << "Adding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
      chain->Add(opt->getArgv(ii));
    }
    chain2->SetName("raw_events_N");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      chain2->Add(opt->getArgv(ii));
    }
    chain->AddFriend(chain2);
  }
  else if (board == 7)
  {
    chain->SetName("raw_events_O");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      std::cout << "Adding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
      chain->Add(opt->getArgv(ii));
    }
    chain2->SetName("raw_events_P");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      chain2->Add(opt->getArgv(ii));
    }
    chain->AddFriend(chain2);
  }

  int entries = chain->GetEntries();
  int first_event = 0;

  if (entries == 0)
  {
    std::cout << "Error: no file or empty file" << std::endl;
    return 2;
  }
  std::cout << "This run has " << entries << " entries" << std::endl;

  if (first_event > entries)
  {
    std::cout << "Error: first event is greater than the number of entries" << std::endl;
    return 2;
  }

  if (opt->getValue("nevents")) // to process only the first "nevents" events in the chain
  {
    unsigned int temp_entries = atoi(opt->getValue("nevents"));
    if (temp_entries < entries)
    {
      entries = temp_entries;
    }
  }
  if (opt->getValue("first_event")) // to choose the first event to process
  {
    first_event = atoi(opt->getValue("first_event"));
  }
  std::cout << "Processing " << entries << " entries, starting from event " << first_event << std::endl;

  // Read raw event from input chain TTree
  if (side && !newDAQ)
  {
    std::cout << "Error: version selected does not contain side " << side << std::endl; // only the new DAQ files contain more than 1 TTree (so side and board > 0)
    return 2;
  }

  std::vector<unsigned int> *raw_event = 0; // buffer vector for the raw event in the TTree
  TBranch *RAW = 0;

  if (board == 0)
  {
    chain->SetBranchAddress("RAW Event", &raw_event, &RAW);

    if (side == 1)
    {
      chain->SetBranchAddress("RAW Event B", &raw_event, &RAW);
    }
  }
  else if (board == 1)
  {
    chain->SetBranchAddress("RAW Event C", &raw_event, &RAW);

    if (side == 1)
    {
      chain->SetBranchAddress("RAW Event D", &raw_event, &RAW);
    }
  }
  else if (board == 2)
  {
    chain->SetBranchAddress("RAW Event E", &raw_event, &RAW);

    if (side == 1)
    {
      chain->SetBranchAddress("RAW Event F", &raw_event, &RAW);
    }
  }
  else if (board == 3)
  {
    chain->SetBranchAddress("RAW Event G", &raw_event, &RAW);

    if (side == 1)
    {
      chain->SetBranchAddress("RAW Event H", &raw_event, &RAW);
    }
  }
  else if (board == 4)
  {
    chain->SetBranchAddress("RAW Event I", &raw_event, &RAW);

    if (side == 1)
    {
      chain->SetBranchAddress("RAW Event J", &raw_event, &RAW);
    }
  }
  else if (board == 5)
  {
    chain->SetBranchAddress("RAW Event K", &raw_event, &RAW);

    if (side == 1)
    {
      chain->SetBranchAddress("RAW Event L", &raw_event, &RAW);
    }
  }
  else if (board == 6)
  {
    chain->SetBranchAddress("RAW Event M", &raw_event, &RAW);

    if (side == 1)
    {
      chain->SetBranchAddress("RAW Event N", &raw_event, &RAW);
    }
  }
  else if (board == 7)
  {
    chain->SetBranchAddress("RAW Event O", &raw_event, &RAW);

    if (side == 1)
    {
      chain->SetBranchAddress("RAW Event P", &raw_event, &RAW);
    }
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

  TFile *foutput = new TFile(output_filename + "_board_" + board + "_side_" + side + ".root", "RECREATE");
  foutput->cd();

  std::vector<cluster> result; // Vector of resulting clusters

  // add t_clusters TTree to output file
  TTree *t_clusters = new TTree("t_clusters", "t_clusters");
  t_clusters->Branch("clusters", &result);

  // Read Calibration file
  if (!opt->getValue("calibration"))
  {
    std::cout << "Error: no calibration file" << std::endl;
    return 2;
  }

  calib cal; // calibration struct
  read_calib(opt->getValue("calibration"), &cal);

  // histos for dynamic calibration
  TH1D *hADC[NChannels];
  TH1D *hADC_CN[NChannels];
  for (int ch = 0; ch < NChannels; ch++)
  {
    hADC[ch] = new TH1D(Form("pedestal_channel_%d_board_%d_side_%d", ch, board, side), Form("Pedestal %d", ch), 50, 0, -1);
    hADC[ch]->GetXaxis()->SetTitle("ADC");
    hADC_CN[ch] = new TH1D(Form("cn_channel_%d_board_%d_side_%d", ch, board, side), Form("CN %d", ch), 50, 0, -1);
    hADC_CN[ch]->GetXaxis()->SetTitle("ADC");
  }

  // Loop over events
  int perc = 0;   // percentage of processed events
  int maxADC = 0; // max ADC in all the events, to set proper graph/histo limits
  int maxEVT = 0; // event where maxADC was found
  int maxPOS = 0; // position of the strip with value maxADC

  for (int index_event = 0; index_event < entries; index_event++) // looping on the events
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
        hADC_CN[ch]->Reset();
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

            if (dynped)
            {
              hADC[i]->Fill(raw_event->at(i)); // if dynped is enabled we keep in memory the raw value to
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
        if (cn != -999 && abs(cn) < maxCN)
        {
          hCommonNoiseVsVA->Fill(cn, va);

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

      if (*max_element(signal.begin(), signal.end()) > 4096) // 4096 is the maximum ADC value possible, any more than that means the event is corrupted
        continue;

      if (*max_element(signal.begin(), signal.end()) > maxADC) // searching for the highest ADC value
      {
        maxADC = *max_element(signal.begin(), signal.end());
        maxEVT = index_event;
        std::vector<float>::iterator it = std::find(signal.begin(), signal.end(), maxADC);
        maxPOS = std::distance(signal.begin(), it);
      }

      if (verbose)
        std::cout << "Highest strip: " << *max_element(signal.begin(), signal.end()) << std::endl;

      hHighest->Fill(*max_element(signal.begin(), signal.end()));

      result = clusterize(&cal, &signal, highthreshold, lowthreshold, // clustering function
                          symmetric, symmetricwidth, absolute);

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

  Double_t norm = hADCCluster->GetEntries();
  hADCCluster->Scale(1 / norm);
  hADCCluster->Write();

  hHighest->Write();

  norm = hADCClusterEdge->GetEntries();
  hADCClusterEdge->Scale(1 / norm);
  hADCClusterEdge->Write();

  norm = hADCCluster1Strip->GetEntries();
  hADCCluster1Strip->Scale(1 / norm);
  hADCCluster1Strip->Write();

  norm = hADCCluster2Strip->GetEntries();
  hADCCluster2Strip->Scale(1 / norm);
  hADCCluster2Strip->Write();

  norm = hADCClusterManyStrip->GetEntries();
  hADCClusterManyStrip->Scale(1 / norm);
  hADCClusterManyStrip->Write();

  hEtaVsADC->Write();

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

  nclus_event->SetTitle("nClus vs nEvent");
  nclus_event->GetXaxis()->SetTitle("# event");
  nclus_event->GetYaxis()->SetTitle("# clusters");
  nclus_event->SetMarkerColor(kRed + 1);
  nclus_event->SetLineColor(kRed + 1);
  nclus_event->SetMarkerSize(0.5);
  nclus_event->Draw("*lSAME");
  nclus_event->Write();

  t_clusters->Write();

  foutput->Close();
  return 0;
}
