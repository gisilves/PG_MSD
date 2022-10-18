#include "event.h"

int PrintCluster(cluster clus)
{
  std::cout << "######## Cluster Info ########" << std::endl;

  std::cout << "Address: " << clus.address << std::endl;
  std::cout << "Width: " << clus.width << std::endl;
  std::cout << "Strips over seed threshold: " << clus.over << std::endl;
  std::cout << "ADC content: " << std::endl;
  std::cout << "Board: " << clus.board << std::endl;
  std::cout << "Side: " << clus.side << std::endl;
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
int GetClusterSide(cluster clus) { return clus.side; }
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

float GetClusterSeedADC(cluster clus, calib *cal)
{
  int seed_idx = GetClusterSeedIndex(clus, cal);

  return clus.ADC.at(seed_idx);
}

int GetClusterVA(cluster clus, calib *cal)
{
  int seed = GetClusterSeed(clus, cal);

  return seed / 64;
}

float GetCN(std::vector<float> *signal, int va, int type) // common mode noise calculation with 3 possible algos: done on a VA (readout ASIC) base
{
  float mean = 0;
  float rms = 0;
  float cn = 0;
  int cnt = 0;

  mean = TMath::Mean(signal->begin() + (va * 64), signal->begin() + (va + 1) * 64);
  rms = TMath::RMS(signal->begin() + (va * 64), signal->begin() + (va + 1) * 64);

  if (type == 0) // simple common noise wrt VA mean ADC value
  {
    for (int i = (va * 64); i < (va + 1) * 64; i++)
    {
      if (signal->at(i) > mean - 2 * rms && signal->at(i) < mean + 2 * rms)
      {
        cn += signal->at(i);
        cnt++;
      }
    }

    if (cnt != 0)
    {
      return cn / cnt;
    }
    else
    {
      return -999;
    }
  }
  else if (type == 1) // Common noise with fixed threshold to exclude potential real signal strips
  {
    for (int i = (va * 64); i < (va + 1) * 64; i++)
    {
      if (signal->at(i) < MIP_ADC / 2) // very conservative cut: half the value expected for a Minimum Ionizing Particle
      {
        cn += signal->at(i);
        cnt++;
      }
    }
    if (cnt != 0)
    {
      return cn / cnt;
    }
    else
    {
      return -999;
    }
  }
  else // Common Noise with 'self tuning' threshold: we use the some of the channels to calculate a baseline level, then we use all the strips in a band around that value to compute the CN
  {
    float hard_cm = 0;
    int cnt2 = 0;
    for (int i = (va * 64 + 8); i < (va * 64 + 23); i++)
    {
      if (signal->at(i) < 1.5 * MIP_ADC) // looser constraint than algo 2
      {
        hard_cm += signal->at(i);
        cnt2++;
      }
    }
    if (cnt2 != 0)
    {
      hard_cm = hard_cm / cnt2;
    }
    else
    {
      return -999;
    }

    for (int i = (va * 64 + 23); i < (va * 64 + 55); i++)
    {
      if (signal->at(i) > hard_cm - 2 * rms && signal->at(i) < hard_cm + 2 * rms) // we use only channels with a value around the baseline calculated at the previous step
      {
        cn += signal->at(i);
        cnt++;
      }
    }
    if (cnt != 0)
    {
      return cn / cnt;
    }
    else
    {
      return -999;
    }
  }
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

bool GoodCluster(cluster clus, calib *cal) // cluster is good if all the strips are "good" in the calibration
{
  bool good = true;
  int pos = 0;

  while (good && pos < clus.width)
  {
    if (cal->status.at(clus.address + pos) == 0)
    {
      pos++;
    }
    else
    {
      good = false;
    }
  }
  return good;
}

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

std::vector<calib> read_calib_all(const char *calib_file, bool verb) //TODO:scrivere bene e aggiornare gli altri eseguibili per usare la nuova calib single file
{
  std::ifstream in;
  in.open(calib_file);

  std::cout << "Reading calibration file " << calib_file << std::endl;

  if (!in.is_open())
    exit(1);

  char comma;
  std::string dummyLine;

  Float_t strip, va, vachannel, ped, rawsigma, sigma, status, not_used;
  Int_t read_channels = 0;
  Int_t detector = 0;

  std::vector<calib> calib_vec;
  calib temp_calib;


  //skip lines starting with #
  while (in.peek() == '#')
  {
    getline(in, dummyLine);
  }

  while (in.good())
  {
    if(in.peek() == '#' || in.peek() == EOF)
    {
      if(read_channels != 0)
      {
        if (verb)
        {
          std::cout << "Read " << read_channels << " channels from calib file" << std::endl;
          std::cout << "Last line: " << strip << " " << va << " " << vachannel << " " << ped << " " << rawsigma << " " << sigma << " " << status << " " << not_used << std::endl;
        }
        calib_vec.push_back(temp_calib);
        temp_calib.ped.clear();
        temp_calib.rsig.clear();
        temp_calib.sig.clear();
        temp_calib.status.clear();
        read_channels = 0;
        detector++;
      }
      else
      {
        getline(in, dummyLine);
      }
    }
    else
    {
      //tokenize line
      std::string line;
      getline(in, line);
      std::stringstream ss(line);
      std::string token;
      std::vector<std::string> tokens;
      while (std::getline(ss, token, ','))
      {
        tokens.push_back(token);
      }

      //read values
      strip = std::stof(tokens[0]);
      va = std::stof(tokens[1]);
      vachannel = std::stof(tokens[2]);
      ped = std::stof(tokens[3]);
      rawsigma = std::stof(tokens[4]);
      sigma = std::stof(tokens[5]);
      status = std::stof(tokens[6]);
      not_used = std::stof(tokens[7]);


      //fill calib struct
      temp_calib.ped.push_back(ped);
      temp_calib.rsig.push_back(rawsigma);
      temp_calib.sig.push_back(sigma);
      temp_calib.status.push_back(status);
      read_channels++;
    }
  }
  
  in.close();
  return calib_vec;
}

std::vector<std::pair<float, bool>> read_alignment(const char *alignment_file) // read ASCII alignment file
{
  std::ifstream in;
  in.open(alignment_file);

  // read alignment parameters (sensor, correction, mirror)
  std::vector<std::pair<float, bool>> alignment;
  std::pair<float, bool> parameters;
  uint dummy;

  std::string bufferLine;

  // read lines not beginning with #
  while (in.good())
  {
    getline(in, bufferLine);
    if (bufferLine[0] != '#' && bufferLine.size() != 0)
    {
      std::stringstream ss(bufferLine);
      ss >> dummy >> parameters.first >> parameters.second;
      alignment.push_back(parameters);
    }
  }

  in.close();
  return alignment;
}

std::vector<cluster> clusterize_event(calib *cal, std::vector<float> *signal,
                                      float highThresh, float lowThresh,
                                      bool symmetric, int symmetric_width,
                                      bool absoluteThresholds = false,
                                      int board = 0,
                                      int side = 0,
                                      bool verbose = false)
{
  int nclust = 0;
  std::vector<cluster> clusters; // Vector returned with all found clusters
  cluster new_cluster;           // Struct for a new cluster to add to the results

  std::vector<int> candidate_seeds; // candidate "seeds" are defined as strips with a value higher than the high_threshold (defined in terms or S/N or absolute value)
  std::vector<int> seeds;           // some of the candidate seed might actually be part of the same cluster: seed is redefined after the cluster is constructed

  if (highThresh < lowThresh)
  {
    if (verbose)
    {
      std::cout << "Warning: Low Threshold is bigger than High Threshold, assuming they are swapped" << std::endl;
    }
    float temp = lowThresh;
    lowThresh = highThresh;
    highThresh = temp;
  }

  for (uint i = 0; i < signal->size(); i++)
  {
    if (absoluteThresholds) // Thresholds are in units of ADC
    {
      if (signal->at(i) > highThresh && cal->status.at(i) == 0)
      {
        candidate_seeds.push_back(i); // Potential cluster seeds
      }
    }
    else // Thresholds are in units of S/N
    {
      if (signal->at(i) / cal->sig.at(i) > highThresh && cal->status.at(i) == 0)
      {
        candidate_seeds.push_back(i);
      }
    }
  }

  if (candidate_seeds.size() != 0)
  {
    seeds.push_back(candidate_seeds.at(0));

    if (candidate_seeds.size() > 1)
    {
      for (uint i = 1; i < candidate_seeds.size(); i++)
      {
        if (std::abs(candidate_seeds.at(i) - candidate_seeds.at(i - 1)) != 1) // Removing adjacent candidate seeds for the cluster: keeping only the first, the second will be naturally part of the cluster at the end
        {
          seeds.push_back(candidate_seeds.at(i));
        }
      }
    }
    if (seeds.size() > maxClusters || seeds.size() == 0) // cut on max number of clusters
    {
      throw "Error: too many or no seeds. ";
    }
  }

  if (verbose)
  {
    std::cout << "Candidate seeds " << candidate_seeds.size() << std::endl;
    std::cout << "Real seeds " << seeds.size() << std::endl;
  }

  if (seeds.size() != 0)
  {
    for (uint current_seed_numb = 0; current_seed_numb < seeds.size(); current_seed_numb++) // looping on all the cluster seeds
    {

      // starting from the seed strip we look to both its right and its left to find strips to add to the cluster
      bool overThreshL = true;
      bool overThreshR = true;
      int overSEED = 1;
      int L = 0;
      int R = 0;
      //

      int width = 0;                 // cluster width
      std::vector<float> clusterADC; // ADC value of strips in the clusters

      if (symmetric) // Cluster is defined as a fixed number of strips neighboring the seed
      {
        if (seeds.at(current_seed_numb) - symmetric_width > 0 && (uint)(seeds.at(current_seed_numb) + symmetric_width) < signal->size())
        {
          std::copy(signal->begin() + (seeds.at(current_seed_numb) - symmetric_width),
                    signal->begin() + (seeds.at(current_seed_numb) + symmetric_width) + 1,
                    back_inserter(clusterADC));

          if (std::accumulate(clusterADC.begin(), clusterADC.end(), 0) > 0)
          {

            new_cluster.address = seeds.at(current_seed_numb) - symmetric_width;
            new_cluster.width = 2 * symmetric_width + 1;
            new_cluster.ADC = clusterADC;
            new_cluster.over = -999;
            new_cluster.board = board;
            new_cluster.side = side;
            clusters.push_back(new_cluster);
          }
        }
        else // Cluster can't be contained in the detector
        {
          continue;
        }
      }
      else
      {
        while (overThreshL) // Will move to the left of the seed
        {
          int stripL = seeds.at(current_seed_numb) - L - 1;
          if (stripL < 0) // we are outside the detector
          {
            overThreshL = false;
            continue;
          }

          if (verbose)
          {
            std::cout << "Seed " << seeds.at(current_seed_numb) << " stripL " << stripL << " status " << cal->status.at(stripL) << std::endl;
          }

          if (cal->status.at(stripL) == 0) // strip is good according to calibration
          {
            float value = signal->at(stripL);
            if (!absoluteThresholds)
            {
              value = value / cal->sig.at(stripL); // value in terms of S/N
            }

            if (value > lowThresh) // strip is over the lower threshold, we will add it to the cluster
            {
              L++;
              if (verbose)
              {
                std::cout << "Strip is over Lthresh" << std::endl;
              }
              if (value > highThresh) // strip is also over the higher threshold, it could actually be the real seed of the cluster
              {
                overSEED++; // we keep track of how many strips are over the higher threshold
              }
            }
            else
            {
              overThreshL = false;
            }
          }
          else
          {
            overThreshL = false;
          }
        }

        while (overThreshR) // Will move to the right of the seed, everything is the same as the previous step
        {
          uint stripR = seeds.at(current_seed_numb) + R + 1;
          if (stripR >= signal->size())
          {
            overThreshR = false;
            continue;
          }

          if (verbose)
          {
            std::cout << "Seed " << seeds.at(current_seed_numb) << " stripR " << stripR << " status " << cal->status.at(stripR) << std::endl;
          }

          if (cal->status.at(stripR) == 0)
          {
            float value = signal->at(stripR);

            if (!absoluteThresholds)
            {
              value = value / cal->sig.at(stripR);
            }

            if (value > lowThresh)
            {
              R++;
              if (verbose)
              {
                std::cout << "Strip is over Lthresh" << std::endl;
              }
              if (value > highThresh)
              {
                overSEED++;
              }
            }
            else
            {
              overThreshR = false;
            }
          }
          else
          {
            overThreshR = false;
          }
        }

        std::copy(signal->begin() + (seeds.at(current_seed_numb) - L),
                  signal->begin() + (seeds.at(current_seed_numb) + R) + 1,
                  back_inserter(clusterADC)); // we copy the strips that are part of the cluster to the buffer vector

        if (std::accumulate(clusterADC.begin(), clusterADC.end(), 0) > 0)
        {
          // setting parameters to the correct value in the cluster struct
          new_cluster.address = seeds.at(current_seed_numb) - L;
          new_cluster.width = (R + L) + 1;
          new_cluster.ADC = clusterADC;
          new_cluster.over = overSEED;
          new_cluster.board = board;
          new_cluster.side = side;
          clusters.push_back(new_cluster); // adding new cluster to cluster result vector

          nclust++;

          if (verbose)
          {
            std::cout << "Add: " << seeds.at(current_seed_numb) - L << " Width: " << (R + L) + 1 << std::endl;
            std::cout << std::endl;
          }
          std::fill(signal->begin() + (seeds.at(current_seed_numb) - L),
                    signal->begin() + (seeds.at(current_seed_numb) + R) + 1,
                    0);
        }
      }
    }
  }
  return clusters;
}
