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
#include "Logger.h"

#define MIP_ADC 16 // 50ADC: DAMPE 300um 15ADC:FOOT 150um
#define maxClusters 100

// each branch has a vector, that corresponds to the list of channels
// let's have the concept of event. Will go in a class

class Event {
  public: 
    // variables
    int nDetectors = 4; // TODO improve this
    int nChannels = 384; // TODO improve this
    std::vector <std::vector <float>> peak; // could be int?
    std::vector <std::vector <float>> baseline {};
    std::vector <std::vector <float>> sigma {};

    // save triggered hits as vector of pairs det and ch
    std::vector <std::pair<int, int>> triggeredHits;
    bool extractedTriggeredHits = false;

    int nsigma = 20; // to consider something a valid hit

    // constructor and destructor
    Event() {}
    ~Event() {}

    // setters
    void SetBaseline(std::vector <std::vector <float>> _baseline) {baseline = _baseline;}
    void SetSigma(std::vector <std::vector <float>> _sigma) {
        sigma = _sigma;
        // for ( int i = 0; i < nDetectors; i++) {
        //     for (int j = 0; j < nChannels; j++) {
        //         float this_sigma = sigma.at(i).at(j);
        //         if (this_sigma < 2 || this_sigma > 5) {
        //             sigma.at(i).at(j) = 3.; // default value
        //         }
        //     }
        // }
    }
    void SetNSigma(int _nsigma) {nsigma = _nsigma;}
    void SetPeak(std::vector <std::vector <float>> _peak) {peak = _peak;}

    // getters
    std::vector <std::vector <float>> GetPeak() {return peak;}
    std::vector <float> GetPeak(int _det) {return peak.at(_det);}
    float GetPeak(int _det, int _channel) {return peak.at(_det).at(_channel);}
    std::vector <std::vector <float>> GetBaseline() {return baseline;}
    float GetBaseline(int _det, int _channel) {return baseline.at(_det).at(_channel);}
    std::vector <std::vector <float>> GetSigma() {return sigma;}
    float GetSigma(int _det, int _channel) {return sigma.at(_det).at(_channel);}
    int GetNsigma() {return nsigma;}
    std::vector <std::pair<int, int>> GetTriggeredHits() {return triggeredHits;}

    // methods 
    // TODO this should have a check that the size of peak is < nDetectors
    void AddPeak(int _detector_number, std::vector <float> _peak_oneDetector) {
        peak.emplace(peak.begin() + _detector_number, _peak_oneDetector);
    } // one vector per detector

    void ExtractTriggeredHits() {
        // loop over all the peaks in here and store the ones that are above nsigma*sigma
        for (int detit = 0; detit < nDetectors; detit++) {
            for (int chit = 0; chit < nChannels; chit++) {                        
                // LogInfo << "peak is " << peak.at(detit)->at(chit) << std::endl;
                // LogInfo << "baseline is " << baseline.at(detit).at(chit) << std::endl;
                // LogInfo << "sigma is " << sigma->at(detit)->at(chit) << std::endl;
                if (GetPeak(detit, chit) - GetBaseline(detit, chit) > nsigma * GetSigma(detit, chit)) {
                    // LogInfo << "DetId " << detit << ", channel " << chit << " triggered" << std::endl;
                    triggeredHits.emplace_back(std::make_pair(detit, chit));
                }
            }
        }
        extractedTriggeredHits = true;
    }

    void PrintValidHits(){
        if (!extractedTriggeredHits) ExtractTriggeredHits();
        for (int hitit = 0; hitit < triggeredHits.size(); hitit++) {
            LogInfo << "DetId " << triggeredHits.at(hitit).first << ", channel " << triggeredHits.at(hitit).second << ", peak: " << GetPeak(triggeredHits.at(hitit).first, triggeredHits.at(hitit).second) << ", baseline: " << GetBaseline(triggeredHits.at(hitit).first, triggeredHits.at(hitit).second) << ", sigma: " << GetSigma(triggeredHits.at(hitit).first, triggeredHits.at(hitit).second) << "\t";
        }
    }

    void PrintInfo() {
        for (int detit = 0; detit < nDetectors; detit++) {
            for (int chit = 0; chit < nChannels; chit++) {
                LogInfo << "DetId " << detit << ", channel " << chit << ", peak: " << GetPeak(detit, chit) << ", baseline: " << GetBaseline(detit, chit) << ", sigma: " << GetSigma(detit, chit) << "\t";
    
            }
            LogInfo << std::endl;
        }
    }

    void PrintOverview() {
        LogInfo << "Number of detectors: " << nDetectors << std::endl;
        LogInfo << "Number of channels: " << nChannels << std::endl;
        LogInfo << "Number of sigmas to consider above threshold: " << nsigma << std::endl;
        LogInfo << "Size of peak: " << peak.size() << std::endl;
        if (peak.size() == 0) return;
        LogInfo << "Size of peak for detector 0: " << peak.at(0).size() << std::endl;
        LogInfo << "Size of peak for detector 1: " << peak.at(1).size() << std::endl;
        LogInfo << "Size of peak for detector 2: " << peak.at(2).size() << std::endl;
        LogInfo << "Size of peak for detector 3: " << peak.at(3).size() << std::endl;
        LogInfo << "Size of baseline: " << baseline.size() << std::endl;
        LogInfo << "Size of sigma: " << sigma.size() << std::endl;
        if (extractedTriggeredHits)
            LogInfo << "Size of triggered hits: " << triggeredHits.size() << std::endl;
    }
};

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

bool read_calib(const char *calib_file, calib *cal, int NChannels, int detector, bool verb);

std::vector<calib> read_calib_all(const char *calib_file, bool verb);

std::vector<std::pair<float, bool>> read_alignment(const char *alignment_file);

std::vector<cluster> clusterize_event(calib *cal, std::vector<float> *signal,
                                      float highThresh, float lowThresh,
                                      bool symmetric, int symmetric_width,
                                      bool absoluteThresholds,
                                      int board,
                                      int side,
                                      bool verbose);

#endif
