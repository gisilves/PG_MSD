#include "TChain.h"
#include "TFile.h"
#include "TError.h"
#include "TH1.h"
#include "TF1.h"
#include "TGraph.h"
#include "TAxis.h"
#include "TCanvas.h"
#include "TLatex.h"
#include "TPDF.h"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <string>
#include "TLine.h"
#include "TKey.h"
#include "TPaveText.h"
#include "anyoption.h"
#include "event.h"

AnyOption *opt; // Handle the option input

int compute_calibration(TChain &chain, TString output_filename, TCanvas &c1, float sigmaraw_cut = 3, float sigma_cut = 6, int board = 0, int side = 0, bool pdf_only = false, bool fast = true, bool fit = false, bool single_file = true, bool last_board = false)
{
  TFile *foutput;
  if (!pdf_only)
  {
    TString root_filename;
    if (!single_file)
    {
      root_filename = output_filename + "_board-" + board + "_side-" + side + ".root";
    }
    else
    {
      root_filename = output_filename + ".root";
    }
    foutput = new TFile(root_filename.Data(), "UPDATE");
    foutput->cd();
  }

  std::string alphabet = "ABCDEFGHIJKLMNOPQRSTWXYZ";
  // Read raw event from input chain TTree
  std::vector<unsigned int> *raw_event = 0;
  TBranch *RAW = 0;

  chain.SetBranchAddress("RAW Event", &raw_event, &RAW);

  chain.GetEntry(0);
  int NChannels = raw_event->size();
  int NVas = NChannels / 64;

  // histos
  TH1D *hADC[NChannels];
  TH1D *hSignal[NChannels];
  TH1D *hCN[NChannels];
  for (int ch = 0; ch < NChannels; ch++)
  {
    hADC[ch] = new TH1D(Form("pedestal_channel_%d_board_%d_side_%d", ch, board, side), Form("Pedestal %d", ch), 1000, 0, -1);
    hADC[ch]->GetXaxis()->SetTitle("ADC");
    hSignal[ch] = new TH1D(Form("signal_channel_%d_board_%d_side_%d", ch, board, side), Form("Signal %d", ch), 1000, -50, 50);
    hSignal[ch]->GetXaxis()->SetTitle("ADC");
    hCN[ch] = new TH1D(Form("cn_channel_%d_board_%d_side_%d", ch, board, side), Form("CN %d", ch), 1000, -50, 50);
    hCN[ch]->GetXaxis()->SetTitle("ADC");
  }

  TF1 *fittedgaus;

  TGraph *gr = new TGraph(NChannels);
  gr->SetName((TString) "Pedestals" + "_board-" + board + "_side-" + side);
  gr->SetTitle("Pedestals");

  TGraph *gr2 = new TGraph(NChannels);
  gr2->SetName((TString) "RawSigma" + "_board-" + board + "_side-" + side);
  gr2->SetTitle("Raw Sigmas");
  gr2->GetXaxis()->SetTitle("channel");
  gr2->GetXaxis()->SetLimits(0, NChannels);

  TGraph *gr3 = new TGraph(NChannels);
  gr3->SetName((TString) "Sigma" + "_board-" + board + "_side-" + side);
  gr3->SetTitle("Sigmas");
  gr3->GetXaxis()->SetTitle("channel");
  gr3->GetXaxis()->SetLimits(0, NChannels);

  std::vector<float> pedestals[NChannels];
  float mean_pedestal = 0;
  float rms_pedestal = 0;
  std::vector<float> rsigma[NChannels];
  float mean_rsigma = 0;
  float rms_rsigma = 0;
  std::vector<float> sigma[NChannels];
  float mean_sigma = 0;
  float rms_sigma = 0;
  float max_sigma = 0;

  char name[100];
  char location[100];
  char bias[100];
  char leak[100];
  char curr6v[100];
  char curr3v[100];
  char delay[100];

  ofstream calfile;
  if (!pdf_only)
  {
    if (!fast)
    {
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
    }
    else
    {
      strcpy(name, (TString) "Board_" + board + (TString) "_Side_" + side);
      strcpy(location, "nd ");
      strcpy(bias, "nd ");
      strcpy(leak, "nd ");
      strcpy(curr6v, "nd ");
      strcpy(curr3v, "nd ");
      strcpy(delay, "nd ");
    }

    std::time_t result = std::time(nullptr);

    if (!single_file)
    {
      calfile.open(output_filename + "_board-" + Form("%d", board) + "_side-" + Form("%d", side) + ".cal");
    }
    else
    {
      calfile.open(output_filename + ".cal", std::ofstream::out | std::ofstream::app);
    }

    calfile << "#temp_SN= NC\n";
    calfile << "#temp_SN= NC\n";
    calfile << "#name= " << name << "\n";
    calfile << "#location= " << location << "\n";
    calfile << "#bias_volt= " << bias << "V\n";
    calfile << "#leak_curr= " << leak << "uA\n";
    calfile << "#6v_curr= " << curr6v << "mA\n";
    calfile << "#3v_curr= " << curr3v << "mA\n";
    calfile << "#starting_time= " << std::asctime(std::localtime(&result));
    calfile << "#temp_right= NC\n";
    calfile << "#temp_left= NC\n";
    calfile << "#hold_delay= " << delay << "\n";
    calfile << "#sigmaraw_cut= " << sigmaraw_cut << "\n";
    calfile << "#sigmaraw_noise_cut= NC\n";
    calfile << "#sigma_cut= " << sigma_cut << "\n";
    calfile << "#sigma_noise_cut= NC\n";
    calfile << "#sigma_k= NC\n";
    calfile << "#occupancy_k= NC\n";
  }

  std::cout << "\nProcessing data for detector on board " << board << " on side " << side << std::endl;
  int entries = chain.GetEntries();
  std::cout << "\tThis run has " << entries << " entries" << std::endl;

  if (entries == 0)
  {
    std::cout << "\tERROR: skipping empty run" << std::endl;
    return -1;
  }

  // First half of events are used to compute pedestals and raw_sigmas
  for (int index_event = 1; index_event < entries / 2; index_event++)
  {
    chain.GetEntry(index_event);
    // if (index_event == 1)
    // {
    //   cout << "Reading event " << index_event << endl;
    //   cout << "\tEvent size " << raw_event->size() << endl;
    // }

    if (raw_event->size() == NChannels)
    {
      for (int k = 0; k < raw_event->size(); k++)
      {
        // Filling histos for each channel for Gaussian Fit
        hADC[k]->Fill(raw_event->at(k));
      }
    }
  }

  for (int ch = 0; ch < NChannels; ch++)
  {
    // Fitting histos with gaus to compute ped and raw_sigma
    if (hADC[ch]->GetEntries())
    {
      if (fit)
      {
        hADC[ch]->Fit("gaus", "QS");
        fittedgaus = (TF1 *)hADC[ch]->GetListOfFunctions()->FindObject("gaus");
        pedestals->push_back(fittedgaus->GetParameter(1));
        rsigma->push_back(fittedgaus->GetParameter(2));
        gr->SetPoint(ch, ch, fittedgaus->GetParameter(1));
        gr2->SetPoint(ch, ch, fittedgaus->GetParameter(2));
      }
      else
      {
        if (ch == 100)
        {
          hADC[ch]->SaveAs("test.root");
        }

        pedestals->push_back(hADC[ch]->GetMean());
        rsigma->push_back(hADC[ch]->GetRMS());
        gr->SetPoint(ch, ch, hADC[ch]->GetMean());
        gr2->SetPoint(ch, ch, hADC[ch]->GetRMS());
      }
    }
    else
    {
      pedestals->push_back(0);
      rsigma->push_back(0);
      gr->SetPoint(ch, ch, 0);
      gr2->SetPoint(ch, ch, 0);
    }
  }

  mean_pedestal = std::accumulate(pedestals->begin(), pedestals->end(), 0.0) / pedestals->size();

  float num_ped = 0;
  for (int i = 0; i < pedestals->size(); i++)
  {
    num_ped += pow(pedestals->at(i) - mean_pedestal, 2);
  }
  rms_pedestal = std::sqrt(num_ped / pedestals->size());

  mean_rsigma = std::accumulate(rsigma->begin(), rsigma->end(), 0.0) / rsigma->size();
  float num_rsigma = 0;
  for (int i = 0; i < rsigma->size(); i++)
  {
    num_rsigma += pow(rsigma->at(i) - mean_rsigma, 2);
  }
  rms_rsigma = std::sqrt(num_rsigma / rsigma->size());

  gr->GetXaxis()->SetTitle("channel");
  TAxis *axis = gr->GetXaxis();
  axis->SetLimits(0, NChannels);
  axis->SetNdivisions(NVas, false);
  c1.cd(1);
  gPad->SetGrid();
  gr->SetMarkerSize(0.8);
  gr->Draw("AL*");

  gr2->GetXaxis()->SetTitle("channel");
  TAxis *axis2 = gr2->GetXaxis();
  axis2->SetLimits(0, NChannels);
  axis2->SetNdivisions(NVas, false);
  c1.cd(2);
  gPad->SetGrid();
  gr2->SetMarkerSize(0.8);
  gr2->Draw("AL*");

  // Like before, but this time we correct for common noise
  for (int index_event = entries / 2; index_event < entries; index_event++)
  {
    chain.GetEntry(index_event);

    if (raw_event->size() == pedestals->size())
    {
      // Pedestal subtraction
      std::vector<float> signal;
      std::transform(raw_event->begin(), raw_event->end(), pedestals->begin(), std::back_inserter(signal), [&](double raw, double ped)
                     { return raw - ped; });

      // Chip-wise CN subtraction before filling the histos
      for (int va = 0; va < NVas; va++) // Loop on VA
      {
        float cn = GetCN(&signal, va, 0);
        if (cn != -999)
        {
          for (int va_chan = 0; va_chan < 64; va_chan++)
          {
            if (signal.size() == NChannels)
            {
              {
                hSignal[64 * va + va_chan]->Fill(signal.at(64 * va + va_chan));
                hCN[64 * va + va_chan]->Fill(signal.at(64 * va + va_chan) - cn);
              }
            }
          }
        }
      }
    }
  }

  // Fitting with gaus to compute sigmas
  int va_chan = 0;
  double sigma_value;

  for (int ch = 0; ch < NChannels; ch++)
  {
    bool badchan = false;
    if (hCN[ch]->GetEntries())
    {
      if (fit)
      {
        hCN[ch]->Fit("gaus", "QS");
        fittedgaus = (TF1 *)hCN[ch]->GetListOfFunctions()->FindObject("gaus");
        gr3->SetPoint(ch, ch, fittedgaus->GetParameter(2));
        sigma->push_back(fittedgaus->GetParameter(2));
        // Flag for channels that are too noisy or dead
        if (rsigma->at(ch) < 1.5 || rsigma->at(ch) > sigmaraw_cut)
        {
          if (fittedgaus->GetParameter(2) < 1 || fittedgaus->GetParameter(2) > sigma_cut)
          {
            badchan = true;
          }
        }
      }
      else
      {
        gr3->SetPoint(ch, ch, hCN[ch]->GetRMS());
        sigma->push_back(hCN[ch]->GetRMS());
        // Flag for channels that are too noisy or dead
        if (rsigma->at(ch) < 1.5 || rsigma->at(ch) > sigmaraw_cut)
        {
          if (hCN[ch]->GetRMS() < 1 || hCN[ch]->GetRMS() > sigma_cut)
          {
            badchan = true;
          }
        }
      }
    }
    else
    {
      gr3->SetPoint(ch, ch, 0);
      rsigma->push_back(0);
      badchan = true;
    }

    if (!pdf_only)
    {
      if (fit)
      {
        sigma_value = fittedgaus->GetParameter(2);
      }
      else
      {
        sigma_value = hCN[ch]->GetRMS();
      }
      // Writing info in .cal file (should be backwards-compatible with miniTRB tools)
      calfile << ch << ", " << ch / 64 << ", "
              << va_chan
              << ", " << pedestals->at(ch) << ", " << rsigma->at(ch) << ", "
              << sigma_value
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
  }
  mean_sigma = std::accumulate(sigma->begin(), sigma->end(), 0.0) / sigma->size();
  rms_sigma = std::sqrt(std::inner_product(sigma->begin(), sigma->end(), sigma->begin(), 0.0) / sigma->size());

  if (!std::isnan(mean_sigma))
  {
    max_sigma = *std::max_element(sigma->begin(), sigma->end());
  }
  else
  {
    max_sigma = 0;
  }

  float num_sigma = 0;
  for (int i = 0; i < sigma->size(); i++)
  {
    num_sigma += pow(sigma->at(i) - mean_sigma, 2);
  }
  rms_sigma = std::sqrt(num_sigma / sigma->size());

  TAxis *axis3 = gr3->GetXaxis();
  axis3->SetLimits(0, NChannels);
  axis3->SetNdivisions(NVas, false);
  c1.cd(3);
  gPad->SetGrid();
  gr3->SetMarkerSize(0.8);
  gr3->Draw("AL*");

  c1.cd(4);
  TPaveText *pt = new TPaveText(.05, .1, .95, .8);

  pt->AddText(Form("Pedestal mean value: %f \t Pedestal RMS value: %f", mean_pedestal, rms_pedestal));
  pt->AddText(Form("Raw sigma mean value: %f \t Raw sigma RMS value: %f", mean_rsigma, rms_rsigma));
  pt->AddText(Form("Sigma mean value: %f \t Sigma RMS value: %f \t Max Sigma: %f", mean_sigma, rms_sigma, max_sigma));
  pt->AddText("Calibration file " + output_filename);
  pt->AddText(Form("Board: %i \t Side: %i", board, side));
  pt->Draw();

  if (board == 0 && !last_board)
  {
    c1.SetGrid();
    c1.Print(output_filename + ".pdf(", "pdf");
  }
  else if (last_board)
  {
    c1.Print(output_filename + ".pdf)", "pdf");
  }
  else
  {
    c1.Print(output_filename + ".pdf", "pdf");
  }

  cout << "\tMean pedestal \t Mean RSigma \t Mean Sigma \t Max Sigma " << endl;
  cout << Form("\t%f \t %f \t %f \t %f", mean_pedestal, mean_rsigma, mean_sigma, max_sigma) << endl;
  cout << "\tRMS pedestal \t RMS RSigma \t RMS Sigma " << endl;
  cout << Form("\t%f \t %f \t %f", rms_pedestal, rms_rsigma, rms_sigma) << endl;

  if (!pdf_only)
  {
    calfile.close();
    gr->Write();
    gr2->Write();
    gr3->Write();
    foutput->Close();
  }

  return 0;
}

int main(int argc, char *argv[])
{
  gErrorIgnoreLevel = kWarning;
  bool verb = false;
  bool pdf_only = false;
  bool fast_mode = false;
  bool fit_mode = false;
  bool single_file = true;

  float sigmaraw_cut = 15;
  float sigma_cut = 10;
  int cntype = 0;

  int NChannels = -1;
  int NVas = -1;

  bool newDAQ = true;
  int side = 0;

  opt = new AnyOption();
  opt->addUsage("Usage: ./calibration [options] [arguments] rootfile");
  opt->addUsage("");
  opt->addUsage("Options: ");
  opt->addUsage("  -h, --help       ................................. Print this help ");
  opt->addUsage("  -v, --verbose    ................................. Verbose ");
  opt->addUsage("  -m, --multiple   ................................. Save calibrations in multiple .cal files (one for each detector)");
  opt->addUsage("  --output         ................................. Output .cal file ");
  opt->addUsage("  --cn             ................................. CN algorithm selection (0,1,2) ");
  opt->addUsage("  --pdf            ................................. PDF only, no .cal file ");
  opt->addUsage("  --fast           ................................. no info prompt");
  opt->addUsage("  --minitrb        ................................. For files acquired with the miniTRB");
  opt->addUsage("  --fit            ................................. Compute calibration parameters with gaussian fits");
  opt->setFlag("help", 'h');
  opt->setFlag("minitrb");
  opt->setFlag("verbose", 'v');
  opt->setFlag("single");
  opt->setFlag("pdf");
  opt->setFlag("fast");
  opt->setFlag("fit");

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

  if (opt->getFlag("help") || opt->getFlag('h'))
    opt->printUsage();

  if (opt->getFlag("verbose") || opt->getFlag('v'))
    verb = true;

  if (opt->getFlag("multiple") || opt->getFlag('m'))
  {
    single_file = false;
  }

  if (opt->getValue("cn"))
    cntype = atoi(opt->getValue("cn"));

  if (opt->getValue("minitrb"))
  {
    newDAQ = false;
    std::cout << "\nminiTRB flag activated" << std::endl;
  }

  if (opt->getValue("pdf"))
  {
    pdf_only = true;
    std::cout << "\nPDF flag activated: no .cal file will be written on disk" << std::endl;
  }

  if (opt->getFlag("fast"))
  {
    fast_mode = true;
    std::cout << "\nFast flag activated: no additional info will be written in the .cal file" << std::endl;
  }

  if (opt->getFlag("single"))
  {
    std::cout << "\nSingle file flag activated: only one .cal file will be written on disk" << std::endl;
  }

  if (opt->getFlag("fit"))
  {
    fit_mode = true;
    std::cout << "\nUsing Gaussian fits to compute calibrations" << std::endl;
  }

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

  int detectors = 0;
  int detector_num = 0;
  int ladder_side = 0;

  // Join ROOTfiles in a single chain
  TChain *chain = new TChain("raw_events"); // Chain input rootfiles
  for (int ii = 0; ii < opt->getArgc(); ii++)
  {
    std::cout << "\nAdding file " << opt->getArgv(ii) << " to the chain..." << std::endl;
    chain->Add(opt->getArgv(ii));
  }

  if (single_file && std::ifstream(output_filename + ".cal"))
  {
    remove(output_filename + ".cal");
  }

  TCanvas *c1 = new TCanvas("calibration", "Canvas", 1920, 1080);
  c1->Divide(2, 2);

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
  std::cout << "File with " << detectors << " detector(s)" << std::endl;
  if (detectors == 1)
    newDAQ = false;

  if (!newDAQ)
  {
    compute_calibration(*chain, output_filename, *c1, sigmaraw_cut, sigma_cut, 0, 0, pdf_only, fast_mode, fit_mode, single_file, true);
  }
  else
  {
    std::cout << "\nNEW DAQ FILE" << std::endl;

    TTree *T;
    TIter list2(tempfile.GetListOfKeys());
    while ((key = (TKey *)list2()))
    {
      if (!strcmp(key->GetClassName(), "TTree"))
      {
        TChain *chain2 = new TChain(key->GetName());

        for (int ii = 0; ii < opt->getArgc(); ii++)
        {
          chain2->Add(opt->getArgv(ii));
        }
        if (detector_num / 2 == detectors / 2 - 1 && ladder_side == 1)
        {
          compute_calibration(*chain2, output_filename, *c1, sigmaraw_cut, sigma_cut, detector_num / 2, ladder_side, pdf_only, fast_mode, fit_mode, single_file, true);
        }
        else
        {
          compute_calibration(*chain2, output_filename, *c1, sigmaraw_cut, sigma_cut, detector_num / 2, ladder_side, pdf_only, fast_mode, fit_mode, single_file, false);
        }
        detector_num++;
        if (ladder_side == 0)
        {
          ladder_side = 1;
        }
        else
        {
          ladder_side = 0;
        }
      }
    }

    tempfile.Close();
  }

  return 0;
}