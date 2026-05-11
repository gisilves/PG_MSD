#pragma once
#include <vector>

struct cluster {
    unsigned short address;
    int width;
    int over;
    std::vector<float> ADC;
    int board;
    int side;
};