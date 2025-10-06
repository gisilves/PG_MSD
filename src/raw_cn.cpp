// raw_cn_cli11.cpp
#include "TChain.h"
#include "TFile.h"
#include "TH1.h"
#include "TH2.h"
#include "TTree.h"
#include "TGraph.h"

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

#include <CLI/CLI.hpp> // modern CLI
#include "event.h"

int main(int argc, char *argv[])
{
  bool verb = false;

  float meanCN = 0;
  int mincn = 0;
  int maxcn = 0;

  int NChannels = 384;
  int NVas = 6;

  // ———————————————— CLI11 ————————————————
  int version = 0;
  std::string output;
  std::string calibration;
  int board = 0;
  int side = 0;
  std::vector<std::string> inputs;

  CLI::App app{"Compute common-noise statistics and pedestals"};
  app.set_help_all_flag("--help-all", "Show all help");

  app.add_flag("-v,--verbose", verb, "Verbose");
  app.add_option("--version", version, "1212 (6VA miniTRB), 1313 (10VA miniTRB), 2020 (PAPERO)")
      ->required();
  app.add_option("--output", output, "Output ROOT file")->required();
  app.add_option("--calibration", calibration, "Calibration file")->required();
  app.add_option("--board", board, "Board number (0,1,2,...)");
  app.add_option("--side", side, "Side number (0,1)");
  app.add_option("inputs", inputs, "Input ROOT files")->required()->expected(-1);

  CLI11_PARSE(app, argc, argv);

  if (version == 1212)
  {
    NChannels = 384;
    NVas = 6;
  }
  else if (version == 1313)
  {
    NChannels = 640;
    NVas = 10;
  }
  else if (version == 2020)
  {
    NChannels = 640;
    NVas = 10;
    if (!app.get_option("--board")->count())
    {
      std::cerr << "ERROR: no board number provided\n";
      return 2;
    }
    if (!app.get_option("--side")->count())
    {
      std::cerr << "ERROR: no side number provided\n";
      return 2;
    }
  }
  else
  {
    std::cerr << "ERROR: invalid miniTRB version\n";
    return 2;
  }

  int minStrip = 0;
  int maxStrip = NChannels - 1;

  // ———————————————— Histos ————————————————
  TH1F *hPedestals = new TH1F("hPedestals", "hPedestals", 1000, 0, 500);
  hPedestals->GetXaxis()->SetTitle("Pedestals");

  TH1F *hCommonNoise0 = new TH1F("hCommonNoise0", "hCommonNoise0", 1000, -50, 50);
  hCommonNoise0->GetXaxis()->SetTitle("CN");
  TH1F *hCommonNoise1 = new TH1F("hCommonNoise1", "hCommonNoise1", 1000, -50, 50);
  hCommonNoise1->GetXaxis()->SetTitle("CN");
  TH1F *hCommonNoise2 = new TH1F("hCommonNoise2", "hCommonNoise2", 1000, -50, 50);
  hCommonNoise2->GetXaxis()->SetTitle("CN");

  TH2F *hCommonNoise0VsVA = new TH2F("hCommonNoise0VsVA", "hCommonNoise0VsVA", 500, -25, 25, 10, -0.5, 9.5);
  hCommonNoise0VsVA->GetXaxis()->SetTitle("CN");
  hCommonNoise0VsVA->GetYaxis()->SetTitle("VA");
  TH2F *hCommonNoise1VsVA = new TH2F("hCommonNoise1VsVA", "hCommonNoise1VsVA", 500, -25, 25, 10, -0.5, 9.5);
  hCommonNoise1VsVA->GetXaxis()->SetTitle("CN");
  hCommonNoise1VsVA->GetYaxis()->SetTitle("VA");
  TH2F *hCommonNoise2VsVA = new TH2F("hCommonNoise2VsVA", "hCommonNoise2VsVA", 500, -25, 25, 10, -0.5, 9.5);
  hCommonNoise2VsVA->GetXaxis()->SetTitle("CN");
  hCommonNoise2VsVA->GetYaxis()->SetTitle("VA");

  TGraph *common_noise_0 = new TGraph();
  TGraph *common_noise_1 = new TGraph();
  TGraph *common_noise_2 = new TGraph();

  std::string alphabet = "ABCDEFGHIJKLMNOPQRSTWXYZ";
  int detector = 2 * board + side;

  TString tree_name = "raw_events";
  if (detector > 0)
  {
    tree_name += "_";
    tree_name += alphabet[detector];
  }
  std::cout << "\tOpening TTree with name: " << tree_name << std::endl;

  TString branch_name = "RAW Event J";
  branch_name += (detector % 2 == 0) ? "5" : "7";
  std::cout << "\t\tReading branch: " << branch_name << std::endl;

  TChain *chain = new TChain(tree_name.Data());
  for (const auto &f : inputs)
  {
    std::cout << "Adding file " << f << " to the chain..." << std::endl;
    chain->Add(f.c_str());
  }

  int entries = chain->GetEntries();
  std::cout << "This run has " << entries << " entries" << std::endl;

  std::vector<unsigned short> *raw_event = nullptr;
  TBranch *RAW = nullptr;
  chain->SetBranchAddress(branch_name.Data(), &raw_event, &RAW);

  TFile *foutput = new TFile(output.c_str(), "RECREATE");
  foutput->cd();

  calib cal;
  bool is_calib = read_calib(calibration.c_str(), &cal, NChannels, detector, verb);
  if (!is_calib)
  {
    std::cout << "ERROR: no calibration file found" << std::endl;
    return 2;
  }

  for (int chan = 0; chan < static_cast<int>(cal.ped.size()); chan++)
  {
    hPedestals->Fill(cal.ped[chan]);
  }

  int perc = 0;
  for (int index_event = 0; index_event < entries; index_event++)
  {
    chain->GetEntry(index_event);

    if (verb)
    {
      std::cout << "\nEVENT: " << index_event << std::endl;
    }

    Double_t pperc = 10.0 * ((index_event + 1.0) / entries);
    if (pperc >= perc)
    {
      std::cout << "Processed " << (index_event + 1) << " out of " << entries
                << ":" << (int)(100.0 * (index_event + 1.0) / entries) << "%" << std::endl;
      perc++;
    }

    std::vector<float> signal(raw_event->size());

    if (raw_event->size() == 384 || raw_event->size() == 640)
    {
      if (cal.ped.size() >= raw_event->size())
      {
        for (size_t i = 0; i != raw_event->size(); i++)
        {
          signal[i] = (raw_event->at(i) - cal.ped[i]);
        }
      }
      else
      {
        if (verb)
          std::cout << "Error: calibration file is not compatible" << std::endl;
      }
    }
    else
    {
      if (verb)
        std::cout << "Error: event " << index_event << " is not complete, skipping it" << std::endl;
      continue;
    }

    meanCN = 0;
#pragma omp parallel for
    for (int va = 0; va < NVas; va++)
    {
      float cn = GetCN(&signal, va, 0);
      if (cn != -999)
      {
#pragma omp critical
        {
          meanCN += cn;
          if (cn < mincn)
            mincn = cn;
          else if (cn > maxcn)
            maxcn = cn;
          hCommonNoise0->Fill(cn);
          hCommonNoise0VsVA->Fill(cn, va);
        }
      }
    }
    meanCN = meanCN / NVas;
    common_noise_0->SetPoint(common_noise_0->GetN(), index_event, meanCN);

    meanCN = 0;
    for (int va = 0; va < NVas; va++)
    {
      float cn = GetCN(&signal, va, 1);
      if (cn != -999)
      {
        meanCN += cn;
        if (cn < mincn)
          mincn = cn;
        else if (cn > maxcn)
          maxcn = cn;
        hCommonNoise1->Fill(cn);
        hCommonNoise1VsVA->Fill(cn, va);
      }
    }
    meanCN = meanCN / NVas;
    common_noise_1->SetPoint(common_noise_1->GetN(), index_event, meanCN);

    meanCN = 0;
    for (int va = 0; va < NVas; va++)
    {
      float cn = GetCN(&signal, va, 2);
      if (cn != -999)
      {
        meanCN += cn;
        if (cn < mincn)
          mincn = cn;
        else if (cn > maxcn)
          maxcn = cn;
        hCommonNoise2->Fill(cn);
        hCommonNoise2VsVA->Fill(cn, va);
      }
    }
    meanCN = meanCN / NVas;
    common_noise_2->SetPoint(common_noise_2->GetN(), index_event, meanCN);
    meanCN = 0;
  }

  hCommonNoise0->Write();
  hCommonNoise1->Write();
  hCommonNoise2->Write();
  hCommonNoise0VsVA->Write();
  hCommonNoise1VsVA->Write();
  hCommonNoise2VsVA->Write();
  hPedestals->Write();
  common_noise_0->Write();
  common_noise_1->Write();
  common_noise_2->Write();

  foutput->Close();

  std::cout << "Min CN: " << mincn << " Max CN: " << maxcn << std::endl;
  return 0;
}
