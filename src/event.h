#ifndef EVENT_H_
#define EVENT_H_

#include "TMath.h"
#include "TROOT.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <unistd.h>
#include <iostream>
#include <numeric>
#include <sstream>

#define MIP_ADC 16 // 50ADC: DAMPE 300um 15ADC:FOOT 150um
#define maxClusters 100

struct cluster
{
  unsigned short address; // first strip of the cluster
  int width;              // width of the cluster
  int over;               // Number of strips over high threshold
  std::vector<float> ADC; // ADC content
  int board;              // board number
  int side;               // side number
};                // Cluster structure

struct calib
{
  std::vector<float> ped;  // pedestals
  std::vector<float> rsig; // raw sigmas (noise)
  std::vector<float> sig;  // sigma (noise after common mode subtraction)
  std::vector<int> status; // status of strip (0 good, !0 bad)
};                   // calibration structure

int PrintCluster(cluster clus);

int GetClusterAddress(cluster clus);
int GetClusterWidth(cluster clus);
int GetClusterOver(cluster clus);
int GetClusterBoard(cluster clus);
int GetClusterSide(cluster clus);
std::vector<float> GetClusterADC(cluster clus);

float GetClusterSignal(cluster clus);

float GetClusterCOG(cluster clus);

int GetClusterSeed(cluster clus, calib *cal);

int GetClusterSeedIndex(cluster clus, calib *cal);

float GetClusterSeedADC(cluster clus, calib *cal);

int GetClusterVA(cluster clus, calib *cal);

float GetCN(std::vector<float> *signal, int va, int type);

float GetClusterSN(cluster clus, calib *cal);

float GetSeedSN(cluster clus, calib *cal);

float GetClusterEta(cluster clus);

float GetPosition(cluster clus, float sensor_pitch);

float GetClusterMIPCharge(cluster clus);

float GetSeedMIPCharge(cluster clus, calib *cal);

bool GoodCluster(cluster clus, calib *cal);

bool read_calib(std::string calib_file, calib *cal, int NChannels, int detector, bool verb);

std::vector<calib> read_calib_all(std::string calib_file, bool verb);

std::vector<std::pair<float, bool>> read_alignment(const char *alignment_file);

std::vector<cluster> clusterize_event(calib *cal, std::vector<float> *signal,
                                      float highThresh, float lowThresh,
                                      bool symmetric, int symmetric_width,
                                      bool absoluteThresholds,
                                      int board,
                                      int side,
                                      bool verbose);

#endif
