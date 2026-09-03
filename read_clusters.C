#include <TFile.h>
#include <TTree.h>
#include <vector>
#include <iostream>

#include "cluster.h"

#include "TH1F.h"
#include "TH2F.h"

// To compile dictionary:
// 1-  rootcling -f cluster_dict.cxx -c cluster.h src/LinkDef.h
// 2-  g++ -shared -fPIC -o libcluster.so cluster_dict.cxx     $(root-config --cflags --libs)

#define MIP_ADC 120 // 50ADC: DAMPE 300um 15ADC:FOOT 150um
float sensor_pitch = 0.108; // in mm

struct calib
{
  std::vector<float> ped;  // pedestals
  std::vector<float> rsig; // raw sigmas (noise)
  std::vector<float> sig;  // sigma (noise after common mode subtraction)
  std::vector<int> status; // status of strip (0 good, !0 bad)
};                   // calibration structure

bool read_calib(const char *calib_file, calib *cal, int NChannels, int detector, bool verb) // read ASCII calib file: based on DaMPE calibration files (multiple detectors in one file)
{
  std::ifstream in;
  in.open(calib_file);

  if (!in.is_open())
    return 0;

  char comma;
  std::string dummyLine;

  Float_t strip, va, vachannel, ped, rawsigma, sigma, status, not_used;
  Int_t read_channels = 0;

  if (detector != 0)
  {
    // skip to line of desired detector
    for (int k = 0; k < (18 + NChannels) * detector; k++)
    {
      getline(in, dummyLine);
    }
  }

  // skip header
  for (int k = 0; k < 18; k++)
  {
    getline(in, dummyLine);
    if (k == 2 && verb)
      std::cout << dummyLine << std::endl;
  }

  // read NChannels
  while (in.good() && read_channels < NChannels)
  {
    in >> strip >> comma >> va >> comma >> vachannel >> comma >> ped >> comma >>
        rawsigma >> comma >> sigma >> comma >> status >> comma >> not_used;

    if (strip >= 0)
    {
      cal->ped.push_back(ped);
      cal->rsig.push_back(rawsigma);
      cal->sig.push_back(sigma);
      cal->status.push_back(status);
      read_channels++;
    }
  }

  if (verb)
  {
    std::cout << "Read " << read_channels << " channels from calib file" << std::endl;
    std::cout << "Last line: " << strip << " " << va << " " << vachannel << " " << ped << " " << rawsigma << " " << sigma << " " << status << " " << not_used << std::endl;
  }
  in.close();
  return 1;
}

int PrintCluster(cluster clus)
{
  std::cout << "######## Cluster Info ########" << std::endl;

  std::cout << "Address: " << clus.address << std::endl;
  std::cout << "Width: " << clus.width << std::endl;
  std::cout << "Strips over seed threshold: " << clus.over << std::endl;
  std::cout << "ADC content: " << std::endl;
  std::cout << "Board: " << clus.board << std::endl;
  for (int idx = 0; idx < clus.width; idx++)
  {
    std::cout << "\t" << idx << ": " << clus.ADC.at(idx) << std::endl;
  }
  std::cout << "##############################" << std::endl;
  std::cout << "Press enter to continue ..." << std::endl;
  std::getchar();
  return 0;
}

int GetClusterAddress(cluster clus) { return clus.address; }
int GetClusterWidth(cluster clus) { return clus.width; }
int GetClusterOver(cluster clus) { return clus.over; }
int GetClusterBoard(cluster clus) { return clus.board; }
std::vector<float> GetClusterADC(cluster clus) { return clus.ADC; }

float GetClusterSignal(cluster clus) // ADC of whole cluster
{
  float signal = 0;
  std::vector<float> ADC = GetClusterADC(clus);

  for (auto &n : ADC)
  {
    signal += n;
  }
  return signal;
}

float GetClusterCOG(cluster clus) // Center Of Gravity of cluster
{
  int address = GetClusterAddress(clus);
  std::vector<float> ADC = GetClusterADC(clus);
  float num = 0;
  float den = 0;
  float cog = -999;

  for (uint i = 0; i < ADC.size(); i++)
  {
    num += ADC.at(i) * (address + i);
    den += ADC.at(i);
  }
  if (den != 0)
  {
    cog = num / den;
  }

  return cog;
}

int GetClusterSeed(cluster clus, calib *cal) // Strip corresponding to the seed
{
  int seed = -999;
  std::vector<float> ADC = GetClusterADC(clus);

  float sn_max = 0; // seed is defined as the strip with highest S/N value

  for (uint i = 0; i < ADC.size(); i++)
  {
    if (ADC.at(i) / cal->sig.at(clus.address + i) > sn_max)
    {
      sn_max = ADC.at(i) / cal->sig.at(clus.address + i);
      seed = clus.address + i;
    }
  }
  return seed;
}

int GetClusterSecond(cluster clus, calib *cal) // Strip corresponding to the second strip by ADC
{
  int seed = GetClusterSeed(clus, cal);
  int second = -999;

  std::vector<float> ADC = GetClusterADC(clus);

  if (seed == clus.address)
  {
    second = clus.address + 1;
  }
  else if (seed == clus.address + clus.width - 1)
  {
    second = clus.address + clus.width - 2;
  }
  else
  {
    if (ADC.at(seed - clus.address - 1) > ADC.at(seed - clus.address + 1))
    {
      second = seed - 1;
    }
    else
    {
      second = seed + 1;
    }
  }

  return second;
}

int GetClusterSeedIndex(cluster clus, calib *cal) // Position of the seed in the cluster
{
  int seed_idx = -999;
  std::vector<float> ADC = GetClusterADC(clus);

  float sn_max = 0;

  for (uint i = 0; i < ADC.size(); i++)
  {
    if (ADC.at(i) / cal->sig.at(clus.address + i) > sn_max)
    {
      sn_max = ADC.at(i) / cal->sig.at(clus.address + i);
      seed_idx = i;
    }
  }
  return seed_idx;
}

int GetClusterSecondIndex(cluster clus, calib *cal)
{
  int seed = GetClusterSeed(clus, cal);
  int second = -999;

  std::vector<float> ADC = GetClusterADC(clus);

  if (ADC.size() != 1)
  {

    if (seed == clus.address)
    {
      second = 1;
    }
    else if (seed == clus.address + clus.width - 1)
    {
      second = clus.width - 2;
    }
    else
    {
      if (ADC.at(seed - clus.address - 1) > ADC.at(seed - clus.address + 1))
      {
        second = seed - clus.address - 1;
      }
      else
      {
        second = seed - clus.address + 1;
      }
    }
  }
  else
  {
    second = -1;
  }

  return second;
}

float GetClusterSeedADC(cluster clus, calib *cal)
{
  int seed_idx = GetClusterSeedIndex(clus, cal);

  return clus.ADC.at(seed_idx);
}

float GetClusterSecondADC(cluster clus, calib *cal)
{
  int second_idx = GetClusterSecondIndex(clus, cal);
  return clus.ADC.at(second_idx);
}

int GetClusterVA(cluster clus, calib *cal)
{
  int seed = GetClusterSeed(clus, cal);

  return seed / 64;
}

float GetClusterSN(cluster clus, calib *cal)
{
  float sn = 0;

  for (int i = 0; i < GetClusterWidth(clus); i++)
  {
    sn += pow(clus.ADC.at(i) / cal->sig.at(i + GetClusterAddress(clus)), 2);
  }

  if (sn > 0)
  {
    return sqrt(sn);
  }
  else
  {
    return -999;
  }
}

float GetSeedSN(cluster clus, calib *cal)
{
  float signal = GetClusterSeedADC(clus, cal);
  float noise = cal->sig.at(GetClusterSeed(clus, cal));

  if (noise)
  {
    return signal / noise;
  }
  else
  {
    return -999;
  }
}

float GetClusterEta(cluster clus)
{
  std::vector<float> ADC = GetClusterADC(clus);

  Int_t nstrips = ADC.size();
  Float_t eta = -999;
  Float_t max_adc = -1;
  Int_t max_pos = 0;

  if (nstrips == 1)
  {
    eta = 1.0;
  }
  else
  {
    max_pos = std::max_element(ADC.begin(), ADC.end()) - ADC.begin();
    max_adc = ADC.at(max_pos);

    if (max_pos == 0)
    {
      eta = ADC.at(0) / (ADC.at(0) + ADC.at(1));
    }
    else if (max_pos == nstrips - 1)
    {
      eta = ADC.at(max_pos - 1) / (ADC.at(max_pos - 1) + ADC.at(max_pos));
    }
    else
    {
      if (ADC.at(max_pos - 1) > ADC.at(max_pos + 1))
      {
        eta = ADC.at(max_pos - 1) / (ADC.at(max_pos - 1) + ADC.at(max_pos));
      }
      else
      {
        eta = ADC.at(max_pos) / (ADC.at(max_pos) + ADC.at(max_pos + 1));
      }
    }
  }

  return eta;
}

float GetPosition(cluster clus, float sensor_pitch) // conversion to mm
{
  float position_mm = GetClusterCOG(clus) * sensor_pitch;
  return position_mm;
}

float GetClusterMIPCharge(cluster clus) // conversion to "Z" charge of the cluster
{
  return sqrt(GetClusterSignal(clus) / MIP_ADC);
}

float GetSeedMIPCharge(cluster clus, calib *cal)
{
  return sqrt(GetClusterSeedADC(clus, cal) / MIP_ADC); // conversion to "Z" charge of the cluster seed
}

bool isClusterinVA(cluster clus) // check if cluster is fully contained in a VA
{
  int firstVA = clus.address / 64;
  int lastVA = (clus.address + clus.width - 1) / 64;

  return (firstVA == lastVA);
}

int read_clusters(TString filename, TString output_filename, int board)
{
    gSystem->Load("./libcluster.so");

    TFile *f = TFile::Open(filename, "READ");
    TTree *t = (TTree *)f->Get(Form("board_%d/t_clusters_board_%d", board, board));
    TString output_file_name = output_filename + "_board" + std::to_string(board).c_str() + ".root";
    TFile *output_file = new TFile(output_file_name, "RECREATE");

    std::vector<cluster> *clusters = nullptr;
    t->SetBranchAddress("clusters", &clusters);


    // Histogram for cluster charge
    TH1F *hClusterCharge = new TH1F((TString) "hClusterCharge_board_" + board, (TString) "hClusterCharge_board_" + board, 1000, -0.5, 25.5);
    hClusterCharge->GetXaxis()->SetTitle("Charge");

    // Histogram for cluster eta
    TH1F *hClusterEta = new TH1F((TString) "hClusterEta_board_" + board, (TString) "hClusterEta_board_" + board, 100, 0, 1);
    hClusterEta->GetXaxis()->SetTitle("Eta");

    // Histogram for cluster position
    TH1F *hClusterPos = new TH1F((TString) "hClusterPos_board_" + board, (TString) "hClusterPos_board_" + board, 500, -0.5, 1792.5);
    hClusterPos->GetXaxis()->SetTitle("strip");

    // Histogram for cluster charge vs position
    TH2F *hClusterChargevsPos = new TH2F((TString) "hClusterChargevsPos_board_" + board, (TString) "hClusterChargevsPos_board_" + board, 500, -0.5, 1792.5, 1000, -0.5, 25.5);
    hClusterChargevsPos->GetXaxis()->SetTitle("strip");
    hClusterChargevsPos->GetYaxis()->SetTitle("Charge");

    Long64_t nEntries = t->GetEntries();
    for (Long64_t i = 0; i < nEntries; i++)
    {
        t->GetEntry(i);
        std::cout << "\rEntry " << i << std::flush;

        // Find the cluster with the highest ADC value
        int maxADC = 0;
        int maxPos = 0;
        if (clusters->size() == 0)
        {
            //std::cout << "No clusters found in this event" << std::endl;
            continue;
        }

        for (int i = 0; i < clusters->size(); i++)
        {
            if (clusters->at(i).ADC.at(0) > maxADC)
            {
                maxADC = clusters->at(i).ADC.at(0);
                maxPos = i;
            }
        }

        // Fill the histogram with the charge of the cluster with the highest ADC value
        hClusterCharge->Fill(GetClusterMIPCharge(clusters->at(maxPos)));
        // Fill the histogram with the eta of the cluster with the highest ADC value
        hClusterEta->Fill(GetClusterEta(clusters->at(maxPos)));
        // Fill the histogram with the position of the cluster with the highest ADC value
        hClusterPos->Fill(GetClusterCOG(clusters->at(maxPos)));
        // Fill the histogram with the charge of the cluster with the highest ADC value vs position
        hClusterChargevsPos->Fill(GetClusterCOG(clusters->at(maxPos)), GetClusterMIPCharge(clusters->at(maxPos)));

        //std::cout << "\nCluster with highest ADC value: " << maxADC << " at position " << maxPos << std::endl;

    }

    hClusterCharge->Write();
    hClusterEta->Write();
    hClusterPos->Write();
    hClusterChargevsPos->Write();

    output_file->Write();
    output_file->Close();
    f->Close();
    return 0;
}