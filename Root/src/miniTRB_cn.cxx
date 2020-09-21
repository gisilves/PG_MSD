#include "TChain.h"
#include "TFile.h"
#include "TH1.h"
#include "TH2.h"
#include "TTree.h"
#include "TGraph.h"
#include "environment.h"

#include <iostream>

#include "anyoption.h"
#include "miniTRB.h"

#if OMP_ == 1
  #include "omp.h"
#endif

AnyOption *opt; //Handle the option input

int main(int argc, char *argv[])
{
  bool verb = false;

  float meanCN = 0;
  int mincn = 0;
  int maxcn = 0;

  int NChannels = 384;
  int NVas = 6;

  opt = new AnyOption();
  opt->addUsage("Usage: ./miniTRB_cn [options] [arguments] rootfile1 rootfile2 ...");
  opt->addUsage("");
  opt->addUsage("Options: ");
  opt->addUsage("  -h, --help       ................................. Print this help ");
  opt->addUsage("  -v, --verbose    ................................. Verbose ");
  opt->addUsage("  --version        ................................. 1212 for 6VA or 1313 for 10VA miniTRB ");
  opt->addUsage("  --output         ................................. Output ROOT file ");
  opt->addUsage("  --calibration    ................................. Calibration file ");

  opt->setFlag("help", 'h');
  opt->setFlag("verbose", 'v');

  opt->setOption("version");
  opt->setOption("output");
  opt->setOption("calibration");

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
    int NChannels = 384;
    int NVas = 6;
    int minStrip = 0;
    int maxStrip = 383;
  }
  else if (atoi(opt->getValue("version")) == 1313)
  {
    int NChannels = 640;
    int NVas = 10;
    int minStrip = 0;
    int maxStrip = 639;
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

  //////////////////Histos//////////////////
  TH1F *hCommonNoise0 = new TH1F("hCommonNoise0", "hCommonNoise0", 1000, -200, 200);
  hCommonNoise0->GetXaxis()->SetTitle("CN");

  TH1F *hCommonNoise1 = new TH1F("hCommonNoise1", "hCommonNoise1", 1000, -200, 200);
  hCommonNoise1->GetXaxis()->SetTitle("CN");

  TH1F *hCommonNoise2 = new TH1F("hCommonNoise2", "hCommonNoise2", 1000, -200, 200);
  hCommonNoise2->GetXaxis()->SetTitle("CN");

  TGraph *common_noise_0 = new TGraph();
  TGraph *common_noise_1 = new TGraph();
  TGraph *common_noise_2 = new TGraph();

  // Join ROOTfiles in a single chain
  TChain *chain = new TChain("raw_events"); //Chain input rootfiles
  for (int ii = 0; ii < opt->getArgc(); ii++)
  {
    std::cout << "Adding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
    chain->Add(opt->getArgv(ii));
  }

  int entries = chain->GetEntries();
  std::cout << "This run has " << entries << " entries" << std::endl;

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

    meanCN = 0;

#pragma omp parallel for //Multithread for loop

    for (int va = 0; va < NVas; va++) //Loop on VA
    {
      float cn = GetCN(&signal, va, 0);

      if (cn != -999)
      {
        meanCN += cn;
        if (cn < mincn)
        {
          mincn = cn;
        }
        else if (cn > maxcn)
        {
          maxcn = cn;
        }
        hCommonNoise0->Fill(cn);
        //hCommonNoiseVsVA->Fill(cn, va);
      }
    }
    meanCN = meanCN / NVas;
    common_noise_0->SetPoint(common_noise_0->GetN(), index_event, meanCN);
    meanCN = 0;

    for (int va = 0; va < NVas; va++) //Loop on VA
    {
      float cn = GetCN(&signal, va, 1);
      if (cn != -999)
      {
        meanCN += cn;

        if (cn < mincn)
        {
          mincn = cn;
        }
        else if (cn > maxcn)
        {
          maxcn = cn;
        }
        hCommonNoise1->Fill(cn);
        //hCommonNoiseVsVA->Fill(cn, va);
      }
    }
    meanCN = meanCN / NVas;
    common_noise_1->SetPoint(common_noise_1->GetN(), index_event, meanCN);
    meanCN = 0;

    for (int va = 0; va < NVas; va++) //Loop on VA
    {
      float cn = GetCN(&signal, va, 2);
      if (cn != -999)
      {
        meanCN += cn;

        if (cn < mincn)
        {
          mincn = cn;
        }
        else if (cn > maxcn)
        {
          maxcn = cn;
        }
        hCommonNoise2->Fill(cn);
        //hCommonNoiseVsVA->Fill(cn, va);
      }
    }
    meanCN = meanCN / NVas;
    common_noise_2->SetPoint(common_noise_2->GetN(), index_event, meanCN);
    meanCN = 0;
  }
  hCommonNoise0->Write();
  hCommonNoise1->Write();
  hCommonNoise2->Write();
  common_noise_0->Write();
  common_noise_1->Write();
  common_noise_2->Write();

  foutput->Close();

  std::cout << "Min CN: " << mincn << " Max CN: " << maxcn << std::endl;

  return 0;
}