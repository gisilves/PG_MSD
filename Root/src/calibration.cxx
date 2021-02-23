#include "TChain.h"
#include "TFile.h"
#include "TH1.h"
#include "TF1.h"
#include "TGraph.h"
#include "TAxis.h"
#include "TCanvas.h"
#include <iostream>
#include "environment.h"
#include <algorithm>
#include <ctime>

#include "anyoption.h"
#include "event.h"

#if OMP_ == 1
#include "omp.h"
#endif

AnyOption *opt; //Handle the option input

int compute_calibration(TChain &chain, TString output_filename, int NChannels, int NVas, float sigmaraw_cut, float sigma_cut, int side)
{
  //histo
  TH1D *hADC[NChannels];
  for (int ch = 0; ch < NChannels; ch++)
  {
    hADC[ch] = new TH1D(Form("pedestal_channel_%d_side_%d", ch, side), Form("Pedestal %d", ch), 1000, 0, -1);
    hADC[ch]->GetXaxis()->SetTitle("ADC");
  }

  TF1 *g;

  TCanvas *c1 = new TCanvas(Form("c1_%d", side), "Canvas", 1920, 1080);
  c1->SetGrid();

  TGraph *gr = new TGraph(NChannels);
  gr->SetTitle("Pedestals");

  TGraph *gr2 = new TGraph(NChannels);
  gr2->SetTitle("Raw Sigma");
  gr2->GetXaxis()->SetTitle("channel");
  gr2->GetXaxis()->SetLimits(0, NChannels);

  TGraph *gr3 = new TGraph(NChannels);
  gr3->SetTitle("Sigma");
  gr3->GetXaxis()->SetTitle("channel");
  gr3->GetXaxis()->SetLimits(0, NChannels);

  std::vector<float> pedestals[NChannels];
  std::vector<float> sigma[NChannels];

  char name[100];
  char location[100];
  char bias[100];
  char leak[100];
  char curr6v[100];
  char curr3v[100];
  char delay[100];

  std::cout << "\n CALIBRATION FILE FOR SIDE " << side << "\n";
  std::cout << "\n Sensor Name: ";
  std::cin >> name;
  std::cout << "\n Location: ";
  std::cin >> location;
  std::cout << "\n Bias (V): ";
  std::cin >> bias;
  std::cout << "\n Leakage current (uA): ";
  std::cin >> leak;
  std::cout << "\n 6V current (mA): ";
  std::cin >> curr6v;
  std::cout << "\n 3V current (mA): ";
  std::cin >> curr3v;
  std::cout << "\n Hold Delay: ";
  std::cin >> delay;
  std::cout << "\n";

  std::time_t result = std::time(nullptr);

  ofstream calfile;
  calfile.open(output_filename + "_" + Form("%d", side) + ".cal");
  calfile << "temp_SN= NC\n";
  calfile << "temp_SN= NC\n";
  calfile << "name= " << name << "\n";
  calfile << "location= " << location << "\n";
  calfile << "bias_volt= " << bias << "V\n";
  calfile << "leak_curr= " << leak << "uA\n";
  calfile << "6v_curr= " << curr6v << "mA\n";
  calfile << "3v_curr= " << curr3v << "mA\n";
  calfile << "starting_time= " << std::asctime(std::localtime(&result)) << "\n";
  calfile << "temp_right= NC\n";
  calfile << "temp_left= NC\n";
  calfile << "hold_delay= " << delay << "\n";
  calfile << "sigmaraw_cut= " << sigmaraw_cut << "\n";
  calfile << "sigmaraw_noise_cut= NC\n";
  calfile << "sigma_cut= " << sigma_cut << "\n";
  calfile << "sigma_noise_cut= NC\n";
  calfile << "sigma_k= NC\n";
  calfile << "occupancy_k= NC\n";

  int entries = chain.GetEntries();
  std::cout << "This run has " << entries << " entries" << std::endl;

  // Read raw event from input chain TTree
  std::vector<unsigned int> *raw_event = 0;
  TBranch *RAW = 0;
  if (side == 0)
  {
    chain.SetBranchAddress("RAW Event", &raw_event, &RAW);
  }
  else
  {
    chain.SetBranchAddress("RAW Event B", &raw_event, &RAW);
  }
  //if (side == 1 && newDAQ)
  //{
  //  chain->SetBranchAddress("RAW Event B", &raw_event, &RAW);
  //}

  //First half of events are used to compute pedestals and raw_sigmas
  for (int index_event = 0; index_event < entries / 2; index_event++)
  {
    chain.GetEntry(index_event);
    if (raw_event->size() == NChannels)
    {
      for (int k = 0; k < raw_event->size(); k++)
      {
        //Filling histos for each channel for Gaussian Fit
        hADC[k]->Fill(raw_event->at(k));
      }
    }
  }

  for (int ch = 0; ch < NChannels; ch++)
  {
    //Fitting histos with gaus to compute ped and raw_sigma
    if (hADC[ch]->GetEntries())
    {
      hADC[ch]->Fit("gaus", "Q");
      g = (TF1 *)hADC[ch]->GetListOfFunctions()->FindObject("gaus");
      pedestals->push_back(g->GetParameter(1));
      sigma->push_back(g->GetParameter(2));
      gr->SetPoint(ch, ch, g->GetParameter(1));
      gr2->SetPoint(ch, ch, g->GetParameter(2));
    }
    else
    {
      pedestals->push_back(0);
      sigma->push_back(0);
      gr->SetPoint(ch, ch, 0);
      gr2->SetPoint(ch, ch, 0);
    }
  }

  gr->GetXaxis()->SetTitle("channel");
  TAxis *axis = gr->GetXaxis();
  axis->SetLimits(0, NChannels);
  axis->SetNdivisions(NVas, false);
  gr->Draw("AL*");
  TString out_pdf_open = output_filename + "_" + Form("%d", side) + ".pdf(";
  c1->Print(out_pdf_open, "pdf");

  TAxis *axis2 = gr2->GetXaxis();
  axis2->SetLimits(0, NChannels);
  axis2->SetNdivisions(NVas, false);
  gr2->Draw("AL*");
  TString out_pdf = output_filename + "_" + Form("%d", side) + ".pdf";
  c1->Print(out_pdf, "pdf");

  //Reset histograms to compute CN corrected noise
  for (int k = 0; k < NChannels; k++)
  {
    hADC[k]->Reset();
  }

  //Like before, but this time we correct for common noise
  for (int index_event = entries / 2; index_event < entries; index_event++)
  {
    chain.GetEntry(index_event);

    if (raw_event->size() == pedestals->size())
    {
      //Pedestal subtraction
      std::vector<float> signal;
      std::transform(raw_event->begin(), raw_event->end(), pedestals->begin(), std::back_inserter(signal), [&](double raw, double ped) {
        return raw - ped;
      });

      //Chip-wise CN subtraction before filling the histos
      for (int va = 0; va < NVas; va++) //Loop on VA
      {
        float cn = GetCN(&signal, va, 0);
        if (cn != -999)
        {
          for (int va_chan = 0; va_chan < 64; va_chan++)
          {
            if (raw_event->size() == NChannels)
            {
              {
                hADC[64 * va + va_chan]->Fill(raw_event->at(64 * va + va_chan) - cn);
              }
            }
          }
        }
      }
    }
  }

  //Fitting with gaus to compute sigmas
  int va_chan = 0;
  for (int ch = 0; ch < NChannels; ch++)
  {
    bool badchan = false;
    if (hADC[ch]->GetEntries())
    {
      hADC[ch]->Fit("gaus", "Q");
      g = (TF1 *)hADC[ch]->GetListOfFunctions()->FindObject("gaus");
      gr3->SetPoint(ch, ch, g->GetParameter(2));

      //Flag for channels that are too noisy or dead
      if (sigma->at(ch) < 0.01 || sigma->at(ch) > sigma_cut)
      {
        if (g->GetParameter(2) < 0.01 || g->GetParameter(2) > sigma_cut)
        {
          badchan = true;
        }
      }
    }
    else
    {
      gr3->SetPoint(ch, ch, 0);
      badchan = true;
    }

    //Writing info in .cal file (should be backwards-compatible with miniTRB tools)
    calfile << ch << ", " << ch / 64 << ", "
            << va_chan
            << ", " << pedestals->at(ch) << ", " << sigma->at(ch) << ", "
            << g->GetParameter(2)
            << ", "
            << badchan
            << ", "
            << "0.000"
            << "\n";
    va_chan++;
    if (va_chan == 64)
    {
      va_chan = 0;
    }
  }

  TAxis *axis3 = gr3->GetXaxis();
  axis3->SetLimits(0, NChannels);
  axis3->SetNdivisions(NVas, false);
  gr3->Draw("AL*");
  TString out_pdf_close = output_filename + "_" + Form("%d", side) + ".pdf)";
  c1->Print(out_pdf_close, "pdf");

  calfile.close();

  return 0;
}

int main(int argc, char *argv[])
{
  bool verb = false;

  float sigmaraw_cut = 5;
  float sigma_cut = 3;
  int cntype = 0;

  int NChannels = -1;
  int NVas = -1;

  bool newDAQ = false;
  int side = 0;

  opt = new AnyOption();
  opt->addUsage("Usage: ./calibration [options] [arguments] rootfile");
  opt->addUsage("");
  opt->addUsage("Options: ");
  opt->addUsage("  -h, --help       ................................. Print this help ");
  opt->addUsage("  -v, --verbose    ................................. Verbose ");
  opt->addUsage("  --version        ................................. 1212 for 6VA miniTRB or 1313 for 10VA miniTRB or 2020 for FOOT DAQ");
  opt->addUsage("  --output         ................................. Output .cal file ");
  opt->addUsage("  --cn             ................................. CN algorithm selection (0,1,2) ");

  opt->setFlag("help", 'h');
  opt->setFlag("verbose", 'v');

  opt->setOption("version");
  opt->setOption("output");
  opt->setOption("cn");

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
  }
  else if (atoi(opt->getValue("version")) == 1313)
  {
    NChannels = 640;
    NVas = 10;
  }
  else if (atoi(opt->getValue("version")) == 2020)
  {
    NChannels = 640;
    NVas = 10;
    newDAQ = true;
  }
  else
  {
    std::cout << "ERROR: invalid DAQ version" << std::endl;
    return 2;
  }

  if (opt->getFlag("help") || opt->getFlag('h'))
    opt->printUsage();

  if (opt->getFlag("verbose") || opt->getFlag('v'))
    verb = true;

  if (opt->getValue("cn"))
    cntype = atoi(opt->getValue("cn"));

  // Create output .cal file
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

  // Join ROOTfiles in a single chain
  TChain *chain = new TChain("raw_events"); //Chain input rootfiles
  for (int ii = 0; ii < opt->getArgc(); ii++)
  {
    std::cout << "Adding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
    chain->Add(opt->getArgv(ii));
  }

  compute_calibration(*chain, output_filename, NChannels, NVas, sigmaraw_cut, sigma_cut, 0);

  if (newDAQ)
  {
    TChain *chain2 = new TChain("raw_events_B");
    for (int ii = 0; ii < opt->getArgc(); ii++)
    {
      chain2->Add(opt->getArgv(ii));
    }

    compute_calibration(*chain2, output_filename, NChannels, NVas, sigmaraw_cut, sigma_cut, 1);
  }

  return 0;
}