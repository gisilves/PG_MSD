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

void display_progress(int current_event, int expected_events, int width = 50)
{
  static bool first_run = true;

  if (!first_run)
  {
    std::cout << "\033[A\r\033[2K"; // Move up, go to start, clear line 1
    std::cout << "\033[2K";         // Clear line 2
  }
  else
  {
    first_run = false;
  }

  // Line 1: Status message
  std::cout << "\tProcessing event " << current_event << " / " << expected_events << "\n";

  // Line 2: Progress bar
  float progress = static_cast<float>(current_event) / static_cast<float>(expected_events);
  int pos = static_cast<int>(width * progress);

  std::cout << "\t[";
  for (int i = 0; i < width; ++i)
  {
    if (i < pos)
      std::cout << "=";
    else if (i == pos)
      std::cout << ">";
    else
      std::cout << " ";
  }
  if (current_event == expected_events)
  {
    std::cout << "] " << static_cast<int>(progress * 100.0) << "%\n\n";
  }
  else
  {
    std::cout << "] " << static_cast<int>(progress * 100.0) << "%";
  }
  std::cout.flush();
}

int read_pedestals(int strip_number, int event_window, int first_event, bool verb,
                   int cntype, std::vector<std::string> input_files,
                   int board, int NChannels, int NVas, float maxCN,
                   int nevents = -1, std::string calibration_file = "", bool silent = false)
{
  //TGraph for pedestal vs event number
  TGraph *pedestal_vs_event = new TGraph();
  TString graph_name = Form("Pedestal board %i for strip %i", board, strip_number);
  pedestal_vs_event->SetName(graph_name);
  pedestal_vs_event->SetTitle(graph_name);

  TChain *chain = new TChain();

  if (board == 0)
  {
    if (!silent)
    {
      std::cout << "\n================================================================================" << std::endl;
      std::cout << "\nWe are on the first detector" << std::endl;
    }
    chain->SetName("raw_events");
  }
  else
  {
    if (!silent)
    {
      std::cout << "\n================================================================================" << std::endl;
      std::cout << "\nWe are on detector " << board << std::endl;
    }
    // Safe naming without out-of-range string indexing
    chain->SetName(Form("raw_events_%c", 'A' + board));
  }

  for (size_t ii = 0; ii < input_files.size(); ii++)
  {
    if (!silent)
      std::cout << "\nAdding file " << input_files[ii] << " to the chain..." << std::endl;
    chain->Add(input_files[ii].c_str());
  }

  int entries = chain->GetEntries();

  if (nevents > 0 && nevents < entries)
  {
    entries = nevents;
  }

  if (entries == 0)
  {
    if (!silent) std::cout << "Error: no file or empty file" << std::endl;
    delete chain;
    return 2;
  }

  if (first_event > entries)
  {
    if (!silent) std::cout << "Error: first event is greater than the number of entries" << std::endl;
    delete chain;
    return 2;
  }

  std::vector<unsigned int> *raw_event = nullptr;
  TBranch *RAW = nullptr;

  if (!silent)
    std::cout << "\nProcessing board " << board << std::endl;
  
  chain->SetBranchAddress("RAW Event", &raw_event, &RAW);

  if (calibration_file.empty())
  {
    if (!silent) std::cout << "Error: no calibration file" << std::endl;
    chain->ResetBranchAddresses();
    delete chain;
    return 2;
  }

  calib cal;
  bool is_calib = read_calib(calibration_file.c_str(), &cal, NChannels, board, verb);

  if (!is_calib)
  {
    if (!silent) std::cout << "ERROR: no calibration file found" << std::endl;
    chain->ResetBranchAddresses();
    delete chain;
    return 2;
  }

  if (!silent)
  {
    std::cout << "\nProcessing " << entries << " entries, starting from event " << first_event << std::endl;
  }

  float signal_avg = 0;
  int events_in_window = 0;
  int averaging_step = 0;

  for (int index_event = first_event; index_event < entries; index_event++)
  {
    int bytes_read = chain->GetEntry(index_event);

    if (bytes_read <= 0 || !raw_event)
    {
      continue;
    }

    if (verb)
    {
      std::cout << "\nEVENT: " << index_event << std::endl;
    }
    display_progress(index_event + 1, entries);

    std::vector<float> signal(raw_event->size());

    if (raw_event->size() == static_cast<size_t>(NChannels))
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
        if (verb) std::cout << "Error: calibration file is not compatible" << std::endl;
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

    if (cntype >= 0)
    {
      for (int va = 0; va < NVas; va++)
      {
        float cn = GetCN(&signal, va, cntype);
        if (verb) std::cout << "VA " << va << " CN " << cn << std::endl;

        if (cn != -999 && std::abs(cn) < maxCN)
        {
          for (int ch = va * 64; ch < (va + 1) * 64; ch++)
          {
            signal[ch] -= cn;
          }
        }
        else
        {
          for (int ch = va * 64; ch < (va + 1) * 64; ch++)
          {
            signal[ch] = -9999;
          }
        }
      }
    }
    
    // If event_window is set, we average the signal over the event_window
    if (event_window > 0)
    {
      signal_avg += signal[strip_number];
      events_in_window++;

      if (events_in_window == event_window + 1)
      {
        signal_avg /= events_in_window;
        pedestal_vs_event->SetPoint(pedestal_vs_event->GetN(), averaging_step, signal_avg);
        averaging_step++;
        signal_avg = 0;
        events_in_window = 0;
      }
    }
  }

  pedestal_vs_event->Write();
  delete pedestal_vs_event;

  // Safe teardown
  chain->ResetBranchAddresses();
  delete chain;
  return 0;
}

int main(int argc, char *argv[])
{
  gErrorIgnoreLevel = kWarning;
  bool verb = false;
  bool silent = false;

  int cntype = 0;
  float maxCN = 999;
  int first_event = 0;
  int nevents = -1;
  
  int strip = 0;
  int event_window = 0;
  int NChannels = 1792;
  int NVas = 28;
  int minStrip = 0;
  int maxStrip = 1791;
  float sensor_pitch = 0.108;
  int minADC_h = 0;
  int maxADC_h = 1000;

  std::vector<std::string> input_files;
  std::string calibration_file, output_file;

  CLI::App app{"raw_pedestals"};

  // Flags
  app.add_flag("-v,--verbose", verb, "Verbose output");
  app.add_flag("--silent", silent, "Silent mode");

  // Options
  app.add_option("--strip", strip, "Strip number");
  app.add_option("--event_window", event_window, "Number of events to average over");
  app.add_option("--cntype", cntype, "Clusterizer type");
  app.add_option("--NChannels", NChannels, "Number of channels");
  app.add_option("--NVas", NVas, "Number of VA chips");
  app.add_option("--minStrip", minStrip, "Minimum strip index");
  app.add_option("--maxStrip", maxStrip, "Maximum strip index");
  app.add_option("--calibration_file", calibration_file, "Path to calibration file")->check(CLI::ExistingFile);
  app.add_option("--output_file", output_file, "Output file name");
  app.add_option("--nevents", nevents, "Number of events to process");
  app.add_option("--first_event", first_event, "First event to process");
  app.add_option("--input_files", input_files, "Input ROOT files")->required()->expected(-1);

  CLI11_PARSE(app, argc, argv);

  // Create output ROOTfile
  TString output_filename;
  if (!output_file.size())
  {
    if (!silent)
      std::cout << "Error: no output file" << std::endl;
    return 2;
  }
  else
  {
    output_filename = TString(output_file);
    if (!silent)
      std::cout << "Output file: " << output_filename << std::endl;
  }

  int detectors = 0;

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
  if (!silent)
    std::cout << "File with " << detectors << " detector(s)" << std::endl;

  TFile *foutput = new TFile(output_filename + ".root", "RECREATE");
  foutput->cd();

  if (detectors == 1)
  {
    read_pedestals(strip, event_window, first_event, verb, cntype, input_files, 0, NChannels, NVas, maxCN, nevents, calibration_file, silent);
  }
  else
  {
    for (int i = 0; i < detectors; i++)
    {
      read_pedestals(strip, event_window, first_event, verb, cntype, input_files, i, NChannels, NVas, maxCN, nevents, calibration_file, silent);
    }
  }

  foutput->Close();
  return 0;
}
