#include "TMath.h"
#include "TROOT.h"
#include <fstream>
#include <iterator>
#include <vector>

#define verbose false
#define SENSOR_PITCH 242
#define MIP_ADC 50
#define maxClusters 10
typedef struct
{
  unsigned short address; //first strip of the cluster
  int width;              //width of the cluster
  int over;               //Number of strips over high threshold
  std::vector<float> ADC; //ADC content
} cluster;                //Cluster structure

typedef struct calib
{
  std::vector<float> ped;
  std::vector<float> rsig;
  std::vector<float> sig;
  std::vector<int> status;
} calib; //calibration structure

int PrintCluster(cluster clus)
{
  std::cout << "######## Cluster Info ########" << std::endl;

  std::cout << "Address: " << clus.address << std::endl;
  std::cout << "Width: " << clus.width << std::endl;
  std::cout << "Strips over seed threshold: " << clus.over << std::endl;
  std::cout << "ADC content: " << std::endl;
  for (size_t idx = 0; idx < clus.width; idx++)
  {
    std::cout << "\t" << idx << ": " << clus.ADC.at(idx) << std::endl;
  }
  std::cout << "##############################" << std::endl;

  return 0;
}

int GetClusterAddress(cluster clus) { return clus.address; }
int GetClusterWidth(cluster clus) { return clus.width; }
std::vector<float> GetClusterADC(cluster clus) { return clus.ADC; }

float GetClusterSignal(cluster clus) //ADC of whole cluster
{
  float signal = 0;
  std::vector<float> ADC = GetClusterADC(clus);

  for (auto &n : ADC)
  {
    signal += n;
  }
  return signal;
}

float GetClusterCOG(cluster clus) //Center Of Gravity of cluster
{
  int address = GetClusterAddress(clus);
  std::vector<float> ADC = GetClusterADC(clus);
  float num = 0;
  float den = 0;
  float cog = -999;

  for (int i = 0; i < ADC.size(); i++)
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

int GetClusterSeed(cluster clus, calib *cal) //Strip corresponding to the seed
{
  int seed = -999;
  std::vector<float> ADC = GetClusterADC(clus);

  float sn_max = 0; //seed is defined as the strip with highest S/N value

  for (size_t i = 0; i < ADC.size(); i++)
  {
    if (ADC.at(i) / cal->sig.at(clus.address + i) > sn_max)
    {
      sn_max = ADC.at(i) / cal->sig.at(clus.address + i);
      seed = clus.address + i;
    }
  }
  return seed;
}

int GetClusterSeedIndex(cluster clus, calib *cal) //Position of the seed in the cluster
{
  int seed_idx = -999;
  std::vector<float> ADC = GetClusterADC(clus);

  float sn_max = 0; //seed is defined as the strip with highest ADC value

  for (size_t i = 0; i < ADC.size(); i++)
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

float GetCN(std::vector<float> *signal, int va, int type)
{
  float mean = 0;
  float rms = 0;
  float cn = 0;
  int cnt = 0;

  mean = TMath::Mean(signal->begin() + (va * 64), signal->begin() + (va + 1) * 64);
  rms = TMath::RMS(signal->begin() + (va * 64), signal->begin() + (va + 1) * 64);

  if (type == 0) //Common noise with respect to VA mean ADC value
  {
    for (size_t i = (va * 64); i < (va + 1) * 64; i++)
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
  else if (type == 1) //Common noise with fixed threshold to exclude signal strips
  {
    for (size_t i = (va * 64); i < (va + 1) * 64; i++)
    {
      if (signal->at(i) < 40)
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
  else //Common Noise with 'self tuning' threshold
  {
    float hard_cm = 0;
    int cnt2 = 0;
    for (size_t i = (va * 64 + 8); i < (va * 64 + 23); i++)
    {
      if (signal->at(i) < 50)
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

    for (size_t i = (va * 64 + 23); i < (va * 64 + 55); i++)
    {
      if (signal->at(i) > hard_cm - 2 * rms && signal->at(i) < hard_cm + 2 * rms)
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

  for (size_t i = 0; i < GetClusterWidth(clus); i++)
  {
    sn += pow(clus.ADC.at(i) / cal->sig.at(i + GetClusterAddress(clus)),2);
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

float GetClusterEta(cluster clus) //Only for clusters with 2 strips
{
  float eta = -999;
  std::vector<float> ADC = GetClusterADC(clus);
  float strip1, strip2;

  if (ADC.size() == 2)
  {
    strip1 = ADC.at(0);
    strip2 = ADC.at(1);

    eta = (strip1 - strip2) / (strip1 + strip2);
  }
  return eta;
}

float GetPosition(cluster clus)
{
  float position_mm = GetClusterCOG(clus) * 0.242;
  return position_mm;
}

float GetClusterMIPCharge(cluster clus)
{
  return sqrt(GetClusterSignal(clus) / MIP_ADC);
}

float GetSeedMIPCharge(cluster clus, calib *cal)
{
  return sqrt(GetClusterSeedADC(clus, cal) / MIP_ADC);
}

bool GoodCluster(cluster clus, calib *cal)
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

int read_calib(char *calib_file, calib *cal)
{

  std::ifstream in;
  in.open(calib_file);

  char comma;

  Float_t strip, va, vachannel, ped, rawsigma, sigma, status, boh;
  Int_t nlines = 0;

  std::string dummyLine;

  for (int k = 0; k < 18; k++)
  {
    getline(in, dummyLine);
  }

  while (in.good())
  {
    in >> strip >> comma >> va >> comma >> vachannel >> comma >> ped >> comma >>
        rawsigma >> comma >> sigma >> comma >> status >> comma >> boh;

    if (strip >= 0)
    {
      cal->ped.push_back(ped);
      cal->rsig.push_back(rawsigma);
      cal->sig.push_back(sigma);
      cal->status.push_back(status);
      nlines++;
    }
  }
  in.close();
  return 0;
}

std::vector<cluster> clusterize(calib *cal, std::vector<float> *signal,
                                float highThresh, float lowThresh,
                                bool symmetric, int symmetric_width,
                                bool absoluteThresholds = false)
{
  int nclust = 0;
  std::vector<cluster> clusters; //Vector returned with all found clusters
  cluster new_cluster;           //Struct for a new cluster to add to the results

  std::vector<int> candidate_seeds;
  std::vector<int> seeds;

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

  for (size_t i = 0; i < signal->size(); i++)
  {
    if (absoluteThresholds) //Thresholds are in units of ADC
    {
      if (signal->at(i) > highThresh && cal->status.at(i) == 0)
      {
        candidate_seeds.push_back(i); //Potential cluster seeds
      }
    }
    else //Thresholds are in units of S/N
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
      for (size_t i = 1; i < candidate_seeds.size(); i++)
      {
        if (std::abs(candidate_seeds.at(i) - candidate_seeds.at(i - 1)) != 1) //Removing adjacent candidate seeds: keeping only the first
        {
          seeds.push_back(candidate_seeds.at(i));
        }
      }
    }
    if (seeds.size() > maxClusters || seeds.size() == 0)
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
    for (size_t seed = 0; seed < seeds.size(); seed++)
    {
      bool overThreshL = true;
      bool overThreshR = true;
      int overSEED = 1;
      int L = 0;
      int R = 0;
      int width = 0;
      std::vector<float> clusterADC;

      float seed_check = signal->at(seeds.at(seed));
      if (!absoluteThresholds)
      {
        seed_check = seed_check / cal->sig.at(seeds.at(seed));
      }
      if (seed_check == 0)
        continue;

      if (symmetric) //Cluster is defined as a fixed number of strips neighboring the seed
      {
        if (seeds.at(seed) - symmetric_width > 0 && seeds.at(seed) + symmetric_width < signal->size())
        {
          std::copy(signal->begin() + (seeds.at(seed) - symmetric_width),
                    signal->begin() + (seeds.at(seed) + symmetric_width) + 1,
                    back_inserter(clusterADC));

          new_cluster.address = seeds.at(seed) - symmetric_width;
          new_cluster.width = 2 * symmetric_width + 1;
          new_cluster.ADC = clusterADC;
          new_cluster.over = -999;
          clusters.push_back(new_cluster);
        }
        else //Cluster can't be contained in the detector
        {
          continue;
        }
      }
      else
      {
        while (overThreshL) //Will move to the left of the seed
        {
          int stripL = seeds.at(seed) - L - 1;
          if (stripL < 0)
          {
            overThreshL = false;
            continue;
          }

          if (verbose)
          {
            std::cout << "Seed " << seeds.at(seed) << " stripL " << stripL << " status " << cal->status.at(stripL) << std::endl;
          }

          if (cal->status.at(stripL) == 0)
          {
            float value = signal->at(stripL);
            if (!absoluteThresholds)
            {
              value = value / cal->sig.at(stripL);
            }

            if (value > lowThresh)
            {
              L++;
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
              overThreshL = false;
            }
          }
          else
          {
            overThreshL = false;
          }
        }

        while (overThreshR) //Will move to the right of the seed
        {
          int stripR = seeds.at(seed) + R + 1;
          if (stripR >= signal->size())
          {
            overThreshR = false;
            continue;
          }

          if (verbose)
          {
            std::cout << "Seed " << seeds.at(seed) << " stripR " << stripR << " status " << cal->status.at(stripR) << std::endl;
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

        std::copy(signal->begin() + (seeds.at(seed) - L),
                  signal->begin() + (seeds.at(seed) + R) + 1,
                  back_inserter(clusterADC));

        new_cluster.address = seeds.at(seed) - L;
        new_cluster.width = (R + L) + 1;
        new_cluster.ADC = clusterADC;
        new_cluster.over = overSEED;
        clusters.push_back(new_cluster);

        nclust++;

        if (verbose)
        {
          std::cout << "Add: " << seeds.at(seed) - L << " Width: " << (R + L) + 1 << std::endl;
          std::cout << std::endl;
        }
        std::fill(signal->begin() + (seeds.at(seed) - L),
                  signal->begin() + (seeds.at(seed) + R) + 1,
                  0);
      }
    }
  }
  return clusters;
}

int seek_endianess(std::fstream &file)
{
  bool found = false;
  bool little_endianess; //Expecting little endian file
  unsigned char buffer[2];
  unsigned short val;

  file.seekg(0); //Start of the binary file

  while (!found && !file.eof())
  {
    file.read(reinterpret_cast<char *>(&buffer), 2);
    val = buffer[0] | (buffer[1] << 8);

    if (val == 0xbbaa)
    {
      if (verbose)
      {
        std::cout << "WARNING: file is BIG Endian, part of the software expects LITTLE Endian" << std::endl;
      }
      little_endianess = false;
      found = true;
    }
    else if (val == 0xaabb)
    {
      if (verbose)
      {
        std::cout << "File is LITTLE Endian" << std::endl;
      }
      little_endianess = true;
      found = true;
    }
  }

  if (!found)
  {
    std::cout << "Can't find endianess WORD in the file" << std::endl;
    return -999;
  }
  else
  {
    return little_endianess;
  }
}

int seek_header(std::fstream &file, bool little_endian)
{
  int offset = 0;
  unsigned short header;
  bool found = false;

  unsigned char buffer[2];
  unsigned short val;

  if (little_endian)
  {
    header = 0xeb90;
  }
  else
  {
    header = 0x90eb;
  }

  while (!found && !file.eof())
  {
    file.seekg(offset);
    file.read(reinterpret_cast<char *>(&buffer), 2);
    val = buffer[0] | (buffer[1] << 8);

    if (val == header)
    {
      found = true;
    }
    else
    {
      offset += 2;
    }
  }

  if (!found)
  {
    std::cout << "Can't find event header in the file" << std::endl;
    return -999;
  }
  else
  {
    return offset;
  }
}

int seek_raw(std::fstream &file, int offset, bool little_endian)
{
  bool found = false;
  bool is_raw = 0;
  unsigned char buffer[2];
  unsigned short val;

  file.seekg(offset + 2);
  file.read(reinterpret_cast<char *>(&buffer), 2);

  if (little_endian)
  {
    val = (int)buffer[1];
  }
  else
  {
    val = (int)buffer[0];
  }

  if (val == 0xa0)
  {
    is_raw = true;
    found = true;
  }

  return is_raw;
}

int seek_version(std::fstream &file)
{
  bool found = false;
  int version;
  unsigned char buffer[2];
  unsigned short val;

  file.seekg(0);

  while (!found && !file.eof())
  {
    file.read(reinterpret_cast<char *>(&buffer), 2);
    val = buffer[0] | (buffer[1] << 8);

    if (val == 0x1212 || val == 0x1313)
    {
      version = val;
      found = true;
    }
  }

  if (!found)
  {
    std::cout << "Can't find version WORD in the file" << std::endl;
    return -999;
  }
  else
  {
    return version;
  }
}

std::vector<unsigned short> read_event(std::fstream &file, int offset,
                                       int version, int evt)
{
  file.seekg(offset + 4 + evt * 1024);

  int event_size;

  if (version == 0x1212)
  {
    event_size = 384;
  }
  else if (version == 0x1313)
  {
    event_size = 640;
  }
  else
  {
    std::cout << "Error: unknown miniTRB version" << std::endl;
  }

  std::vector<unsigned short> buffer(event_size);
  file.read(reinterpret_cast<char *>(buffer.data()), buffer.size() * 2);
  std::streamsize s = file.gcount();
  buffer.resize(s / 2);

  std::vector<unsigned short> event(s / 2);

  int j = 0;
  for (int i = 0; i < buffer.size(); i += 2)
  {
    event.at(j) = buffer.at(i);
    event.at(j + buffer.size() / 2) = buffer.at(i + 1);
    j++;
  }

  return event;
}
