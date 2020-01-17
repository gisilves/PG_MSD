#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numeric>
#include <string>
#include <vector>
#include "Riostream.h"
#include "TFile.h"
#include "TROOT.h"
#include "TString.h"
#include "TTree.h"

typedef struct {
    int address;
    int width;
    std::vector<float> ADC;
} cluster;

typedef struct {
    std::vector<float> ped;
    std::vector<float> rsig;
    std::vector<float> sig;
    std::vector<int> status;
} calib;

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
        in >> strip >> comma >> va >> comma >> vachannel >> comma >> ped >> comma >> rawsigma >> comma >> sigma >> comma >> status >> comma >> boh;

        if (strip >= 0) {
            cal->ped.push_back(ped);
            cal->rsig.push_back(rawsigma);
            cal->sig.push_back(sigma);
            cal->status.push_back(status);
            nlines++;
        }
    }

    //std::cout << "Read " << nlines-1 << " lines" << std::endl;

    in.close();
    return 0;
}

int clusterize(std::vector<float> *signal, float highThresh, float lowThresh, bool symmetric, int symmetric_width, int cn_type = 0, int max_cn = 999) {
    
    std::vector<cluster> clusters;

    int maxClusters = 10;
    int nclust = 0;

    std::vector<int> candidate_seeds;
    std::vector<int> seeds;

    for (size_t i = 0; i < signal->size(); i++) {
        if (signal->at(i) > highThresh) {
            candidate_seeds.push_back(i);
        }
    }

    std::cout << "Candidate seeds: " << candidate_seeds.size() << std::endl;

    if (candidate_seeds.size() != 0) {
        std::vector<int> seeds = {candidate_seeds.at(0)};

        for (size_t i = 1; i < candidate_seeds.size(); i++) {
            if (std::abs(candidate_seeds.at(i) - candidate_seeds.at(i - 1)) != 1) {
                seeds.push_back(candidate_seeds.at(i));
            }
        }

        if (seeds.size() > maxClusters || seeds.size() == 0) {
            std::cout << "Error: too many seeds, check thresholds value (or change maxClusters in code)" << std::endl;
            return 1;
        }

        std::cout << "\"Real\" seeds: " << seeds.size() << std::endl;
    } else {
        return 1;
    }

    //if cn_type >= 0:
    //      cn = common_noise_event(data, cnt, cn_type)
    //      event = event - cn

    if (seeds.size() != 0) {
        nclust = seeds.size();
        for (size_t seed; seed < seeds.size(); seed++) {
            bool overThreshL, overThreshR = true;
            int L, R = 0;
            int width = 0;

            if (symmetric) {
                if (seed - symmetric_width >= 0 && seed + symmetric_width < signal->size()) {
                    
                    /*
                        signal = np.sum(event[seed - symmetric_width:seed +
                                              symmetric_width])
                        width = 2 * symmetric_width + 1
                        cogN = np.sum(event[seed - symmetric_width:seed +
                                            symmetric_width] *
                                      np.arange(seed - symmetric_width,
                                                seed + symmetric_width))
                        cogD = np.sum(event[seed - symmetric_width:seed +
                                            symmetric_width])
                        address = seed
                        */
                } else {
                    continue;
                }
            } else {
                while (overThreshL) {
                    if ((seed - L - 1) > 0) {
                        if (signal->at(seed - L - 1) > lowThresh) {
                            /*
                                signal += event[seed - L - 1]
                                cogN += (seed - L - 1) * event[seed - L - 1]
                                cogD += event[seed - L - 1]
                                address = (seed - L - 1)
                                width += 1
                                L += 1
                                */
                        } else {
                            overThreshL = false;
                        }
                    } else {
                        overThreshL = false;
                    }
                }

                while (overThreshR) {
                    if ((seed + R + 1) < signal->size()) {
                        if (signal->at(seed + R + 1) > lowThresh) {
                            /*
                                signal += event[seed + R + 1]
                                cogN += (seed + R + 1) * event[seed + L + 1]
                                cogD += event[seed + L + 1]
                                R += 1
                                width += 1
                                */
                        } else {
                            overThreshR = false;
                        }
                    } else {
                        overThreshR = false;
                    }
                }
            }
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    std::vector<int> test{334, 324, 361, 364, 340, 367, 351, 330, 302, 337, 332, 363, 374, 391, 375, 382, 380, 396, 373, 378, 399, 369, 353, 365, 345, 384, 430, 382, 377, 376, 375, 351, 314, 341, 351, 328, 355, 308, 381, 358, 363, 343, 344, 353, 359, 340, 366, 321, 346, 335, 366, 326, 337, 305, 318, 326, 353, 316, 317, 308, 267, 304, 302, 276, 319, 377, 364, 374, 399, 377, 386, 361, 370, 354, 379, 347, 382, 389, 345, 361, 342, 343, 389, 369, 343, 354, 361, 337, 385, 365, 397, 366, 371, 382, 404, 366, 417, 406, 380, 400, 403, 406, 408, 379, 411, 418, 402, 388, 415, 371, 376, 389, 429, 427, 427, 419, 426, 396, 425, 393, 411, 431, 417, 394, 447, 414, 421, 400, 366, 373, 390, 376, 358, 372, 371, 378, 362, 363, 381, 341, 366, 343, 384, 304, 352, 379, 347, 330, 352, 366, 320, 342, 345, 352, 316, 346, 348, 377, 318, 376, 357, 371, 330, 316, 349, 373, 345, 302, 308, 328, 337, 353, 327, 386, 355, 364, 368, 374, 384, 372, 405, 353, 371, 340, 383, 390, 351, 364, 381, 351, 406, 379, 208, 220, 242, 229, 243, 240, 239, 232, 206, 251, 255, 270, 264, 279, 299, 243, 253, 294, 283, 272, 251, 260, 277, 250, 233, 283, 299, 283, 278, 263, 258, 284, 249, 255, 280, 276, 248, 277, 244, 260, 260, 252, 260, 246, 279, 236, 252, 205, 260, 225, 257, 252, 203, 244, 266, 224, 230, 209, 220, 260, 242, 234, 206, 158, 205, 229, 185, 225, 201, 201, 211, 234, 226, 224, 207, 166, 261, 227, 216, 246, 229, 232, 254, 236, 239, 254, 257, 217, 284, 279, 288, 232, 290, 305, 264, 282, 238, 298, 238, 298, 284, 283, 283, 292, 259, 281, 264, 302, 243, 267, 287, 251, 251, 271, 273, 285, 230, 225, 202, 199, 190, 251, 199, 200, 201, 174, 179, 199, 214, 217, 232, 211, 225, 256, 206, 209, 206, 255, 229, 290, 240, 251, 255, 262, 213, 260, 268, 242, 242, 291, 236, 216, 256, 265, 246, 282, 296, 307, 318, 265, 277, 293, 319, 319, 321, 293, 287, 319, 336, 332, 401, 316, 356, 348, 334, 355, 314, 385, 411, 366, 368, 348, 354, 355, 351, 347, 359, 364, 348, 320, 350, 379};
    std::vector<float> signal;

    if (argc < 3) {
        std::cout << "Usage: ./test <input calibration file> <threshold>" << std::endl;
        return 1;
    }

    float thresh = atof(argv[2]);

    calib cal;

    read_calib(argv[1], &cal);

    for (std::vector<unsigned short>::size_type i = 0; i != test.size(); i++) {
        if (cal.status[i] == 0) {
            signal.push_back(test[i] - cal.ped[i]);
        } else {
            signal.push_back(0);
        }
    }

    clusterize(&signal, thresh, 20, 0, 0, 0, 999);

    return 0;
}