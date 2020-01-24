#include "TMath.h"
#include "TROOT.h"
#include <fstream>
#include <iterator>
#include <vector>

#define SENSOR_PITCH 242
#define MIP_ADC 60

typedef struct {
  unsigned short address;
  int width;
  float hThresh;
  float lThresh;
  int over;
  std::vector<float> ADC;
} cluster;

typedef struct calib {
  std::vector<float> ped;
  std::vector<float> rsig;
  std::vector<float> sig;
  std::vector<int> status;
} calib;

int GetClusterAddress(cluster clus) { return clus.address; }
int GetClusterWidth(cluster clus) { return clus.width; }
std::vector<float> GetClusterADC(cluster clus) { return clus.ADC; }

float GetClusterSignal(cluster clus) {
  float signal = 0;
  std::vector<float> ADC = GetClusterADC(clus);

  for (auto &n : ADC) {
    signal += n;
  }
  return signal;
}

float GetClusterCOG(cluster clus) {
  float signal;
  int address = GetClusterAddress(clus);
  std::vector<float> ADC = GetClusterADC(clus);
  float num = 0;
  float den = 0;

  for (int i = 0; i < ADC.size(); i++) {
    num += ADC.at(i) * (address + i);
    den += ADC.at(i);
  }
  if (den != 0) {
    return num / den;
  } else {
    return -999;
  }
}

int GetClusterSeed(cluster clus) {
  int seed;
  std::vector<float> ADC = GetClusterADC(clus);
  std::vector<float>::iterator max;

  max = std::max_element(ADC.begin(), ADC.end());

  return clus.address + std::distance(ADC.begin(), max);
}

int GetClusterSeedIndex(cluster clus) {
  int seed;
  std::vector<float> ADC = GetClusterADC(clus);
  std::vector<float>::iterator max;

  max = std::max_element(ADC.begin(), ADC.end());

  return std::distance(ADC.begin(), max);
}

float GetClusterSeedADC(cluster clus) {
  int seed;
  std::vector<float> ADC = GetClusterADC(clus);
  std::vector<float>::iterator max;

  max = std::max_element(ADC.begin(), ADC.end());

  return *max;
}

int GetClusterVA(cluster clus) {
  int seed = GetClusterSeed(clus);

  return seed / 64;
}

float GetCN(std::vector<float> *signal, int va, int type) {
  float mean = 0;
  float rms = 0;
  float cn = 0;
  int cnt = 0;

  mean =
      TMath::Mean(signal->begin() + (va * 64), signal->begin() + (va + 1) * 64);
  rms =
      TMath::RMS(signal->begin() + (va * 64), signal->begin() + (va + 1) * 64);

  // std::cout << "Mean " << mean << std::endl;
  // std::cout << "RMS " << rms << std::endl;

  if (type == 0) {
    for (size_t i = (va * 64); i < (va + 1) * 64; i++) {
      if (signal->at(i) > mean - rms && signal->at(i) < mean + rms) {
        cn += signal->at(i);
        cnt++;
      }
    }
    if (cnt != 0) {
      return cn / cnt;
    } else {
      return -999;
    }
  } else if (type == 1) {
    for (size_t i = (va * 64); i < (va + 1) * 64; i++) {
      if (signal->at(i) < 40) {
        cn += signal->at(i);
        cnt++;
      }
    }
    if (cnt != 0) {
      return cn / cnt;
    } else {
      return -999;
    }
  } else {
    float hard_cm = 0;
    int cnt2 = 0;
    for (size_t i = (va * 64 + 8); i < (va * 64 + 23); i++) {
      if (signal->at(i) < 50) {
        hard_cm += signal->at(i);
        cnt2++;
      }
    }
    if (cnt2 != 0) {
      hard_cm = hard_cm / cnt2;
    } else {
      return -999;
    }

    for (size_t i = (va * 64 + 23); i < (va * 64 + 55); i++) {
      if (signal->at(i) > hard_cm - 10 && signal->at(i) < hard_cm + 10) {
        cn += signal->at(i);
        cnt++;
      }
    }
    if (cnt != 0) {
      return cn / cnt;
    } else {
      return -999;
    }
  }
}

float GetClusterSN(cluster clus, calib *cal) {
  float signal = GetClusterSignal(clus);
  float noise = 0;

  for (size_t i = 0; i < GetClusterWidth(clus); i++) {
    noise += cal->sig.at(i + GetClusterAddress(clus));
  }
  if (noise > 0) {
    return signal / noise;
  } else {
    return -999;
  }
}

float GetSeedSN(cluster clus, calib *cal) {
  float signal = GetClusterSeedADC(clus);
  float noise = cal->sig.at(GetClusterSeed(clus));

  if (noise) {
    return signal / noise;
  } else {
    return -999;
  }
}

float GetClusterEta(cluster clus) {
  float eta = -999;
  std::vector<float> ADC = GetClusterADC(clus);
  if (ADC.size() == 2) {
    // float strip1 = ADC.at(0);
    // float strip2 = ADC.at(1);

    // if(strip1 > strip2){
    eta = (ADC.at(0) - ADC.at(1)) / (ADC.at(0) + ADC.at(1));
    // }else{
    // eta = (ADC.at(1) - ADC.at(0)) / (ADC.at(1) + ADC.at(0));
    // }
  }
  return eta;
}

float GetPosition(cluster clus) {
  float position_mm = GetClusterCOG(clus) / 242.0 * 1000;
  return position_mm;
}

float GetClusterMIPCharge(cluster clus) {
  return sqrt(GetClusterSignal(clus) / MIP_ADC);
}

float GetSeedMIPCharge(cluster clus) {
  return sqrt(GetClusterSeedADC(clus) / MIP_ADC);
}

int read_calib(char *calib_file, calib *cal) {

  std::ifstream in;
  in.open(calib_file);

  char comma;

  Float_t strip, va, vachannel, ped, rawsigma, sigma, status, boh;
  Int_t nlines = 0;

  std::string dummyLine;

  for (int k = 0; k < 18; k++) {
    getline(in, dummyLine);
  }

  while (in.good()) {
    in >> strip >> comma >> va >> comma >> vachannel >> comma >> ped >> comma >>
        rawsigma >> comma >> sigma >> comma >> status >> comma >> boh;

    if (strip >= 0) {
      cal->ped.push_back(ped);
      cal->rsig.push_back(rawsigma);
      cal->sig.push_back(sigma);
      cal->status.push_back(status);
      nlines++;
    }
  }

  // std::cout << "Read " << nlines-1 << " lines" << std::endl;

  in.close();
  return 0;
}

std::vector<cluster> clusterize(calib *cal, std::vector<float> *signal,
                                float highThresh, float lowThresh,
                                bool symmetric, int symmetric_width,
                                bool absoluteThresholds = false) {
  std::vector<cluster> clusters;
  int maxClusters = 5;
  int nclust = 0;
  cluster new_cluster;

  std::vector<int> candidate_seeds;
  std::vector<int> seeds;

  for (size_t i = 0; i < signal->size(); i++) {
    if (absoluteThresholds) {
      if (signal->at(i) > highThresh) {
        candidate_seeds.push_back(i);
      }
    } else {
      if (signal->at(i) / cal->sig.at(i) > highThresh) {
        candidate_seeds.push_back(i);
      }
    }
  }

  if (candidate_seeds.size() != 0) {
    seeds.push_back(candidate_seeds.at(0));

    for (size_t i = 1; i < candidate_seeds.size(); i++) {
      if (std::abs(candidate_seeds.at(i) - candidate_seeds.at(i - 1)) != 1) {
        seeds.push_back(candidate_seeds.at(i));
      }
    }

    if (seeds.size() > maxClusters || seeds.size() == 0) {
      throw "Error: too many seeds. ";
    }
  } else {
    // return 1;
  }

  if (seeds.size() != 0) {
    nclust = seeds.size();

    for (size_t seed = 0; seed < seeds.size(); seed++) {
      bool overThreshL, overThreshR = true;
      int overSEED = 1;
      int L = 0;
      int R = 0;
      int width = 0;
      std::vector<float> clusterADC;

      if (symmetric) {
        if (seeds.at(seed) - symmetric_width > 0 &&
            seeds.at(seed) + symmetric_width < signal->size()) {
          std::copy(signal->begin() + (seeds.at(seed) - symmetric_width),
                    signal->begin() + (seeds.at(seed) + symmetric_width) + 1,
                    back_inserter(clusterADC));

          new_cluster.address = seeds.at(seed) - symmetric_width;
          new_cluster.width = 2 * symmetric_width + 1;
          new_cluster.ADC = clusterADC;
          new_cluster.hThresh = highThresh;
          new_cluster.lThresh = lowThresh;
          new_cluster.over = 1;
          clusters.push_back(new_cluster);
        } else {
          continue;
        }
      } else {
        while (overThreshL) {
          if ((seeds.at(seed) - L - 1) > 0) {
            float value = 0;
            if (absoluteThresholds) {
              value = signal->at(seeds.at(seed) - L - 1);
            } else {
              value = signal->at(seeds.at(seed) - L - 1) /
                      cal->sig.at(seeds.at(seed) - L - 1);
            }

            if (value > lowThresh) {
              L++;
              if (value > highThresh) {
                overSEED++;
              }
            } else {
              overThreshL = false;
            }
          } else {
            overThreshL = false;
          }
        }

        while (overThreshR) {
          if ((seeds.at(seed) + R + 1) < signal->size()) {
            float value = 0;
            if (absoluteThresholds) {
              value = signal->at(seeds.at(seed) + R + 1);
            } else {
              value = signal->at(seeds.at(seed) + R + 1) /
                      cal->sig.at(seeds.at(seed) + R + 1);
            }

            if (value > lowThresh) {
              R++;
              if (value > highThresh) {
                overSEED++;
              }
            } else {
              overThreshR = false;
            }
          } else {
            overThreshR = false;
          }
        }
        std::copy(signal->begin() + (seeds.at(seed) - L),
                  signal->begin() + (seeds.at(seed) + R) + 1,
                  back_inserter(clusterADC));

        new_cluster.address = seeds.at(seed) - L;
        new_cluster.width = (R - L) + 1;
        new_cluster.ADC = clusterADC;
        new_cluster.over = overSEED;
        clusters.push_back(new_cluster);
      }
    }
  }
  return clusters;
}

int seek_endianess(std::fstream &file) {
  bool found = false;
  bool little_endianess;
  unsigned char buffer[2];
  unsigned short val;

  file.seekg(0);

  while (!found && !file.eof()) {
    file.read(reinterpret_cast<char *>(&buffer), 2);
    val = buffer[0] | (buffer[1] << 8);

    if (val == 0xbbaa) {
      little_endianess = false;
      found = true;
    } else if (val == 0xaabb) {
      little_endianess = true;
      found = true;
    }
  }

  if (!found) {
    std::cout << "Can't find endianess WORD in the file" << std::endl;
    return -999;
  } else {
    return little_endianess;
  }
}

int seek_header(std::fstream &file, bool little_endian) {
  int offset = 0;
  unsigned short header;
  bool found = false;

  unsigned char buffer[2];
  unsigned short val;

  if (little_endian) {
    header = 0xeb90;
  } else {
    header = 0x90eb;
  }

  while (!found && !file.eof()) {
    file.seekg(offset);
    file.read(reinterpret_cast<char *>(&buffer), 2);
    val = buffer[0] | (buffer[1] << 8);

    if (val == header) {
      found = true;
    } else {
      offset += 2;
    }
  }

  if (!found) {
    std::cout << "Can't find event header in the file" << std::endl;
    return -999;
  } else {
    return offset;
  }
}

int seek_raw(std::fstream &file, int offset, bool little_endian) {
  bool found = false;
  bool is_raw = 0;
  unsigned char buffer[2];
  unsigned short val;

  file.seekg(offset + 2);
  file.read(reinterpret_cast<char *>(&buffer), 2);

  if (little_endian) {
    val = (int)buffer[1];
  } else {
    val = (int)buffer[0];
  }

  if (val == 0xa0) {
    is_raw = true;
    found = true;
  }

  return is_raw;
}

int seek_version(std::fstream &file) {
  bool found = false;
  int version;
  unsigned char buffer[2];
  unsigned short val;

  file.seekg(0);

  while (!found && !file.eof()) {
    file.read(reinterpret_cast<char *>(&buffer), 2);
    val = buffer[0] | (buffer[1] << 8);

    if (val == 0x1212 || val == 0x1313) {
      version = val;
      found = true;
    }
  }

  if (!found) {
    std::cout << "Can't find version WORD in the file" << std::endl;
    return -999;
  } else {
    return version;
  }
}

std::vector<unsigned short> read_event(std::fstream &file, int offset,
                                       int version, int evt) {
  file.seekg(offset + 4 + evt * 1024);

  int event_size;

  if (version == 0x1212) {
    event_size = 384;
  } else if (version == 0x1313) {
    event_size = 640;
  } else {
    std::cout << "Error: unknown miniTRB version" << std::endl;
  }

  std::vector<unsigned short> buffer(event_size);
  file.read(reinterpret_cast<char *>(buffer.data()), buffer.size() * 2);
  std::streamsize s = file.gcount();
  buffer.resize(s / 2);

  std::vector<unsigned short> event(s / 2);

  int j = 0;
  for (int i = 0; i < buffer.size(); i += 2) {
    event.at(j) = buffer.at(i);
    event.at(j + buffer.size() / 2) = buffer.at(i + 1);
    j++;
  }

  return event;
}
