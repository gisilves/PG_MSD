//
// Created by Nadrino on 04/07/2025.
//

#ifndef BMRAWTOROOTCONVERTER_H
#define BMRAWTOROOTCONVERTER_H

#define ADC_N 10
#define N_DETECTORS 3
#define N_CHANNELS 384

#include <tuple>
#include <cstdint>


struct BeamMonitorEventBuffer {
  bool isGood{false};
  unsigned long eventSize{0};
  unsigned long fwVersion{0};
  unsigned long triggerNumber{0};
  unsigned long boardId{0};
  unsigned long timestamp{0};
  unsigned long extTimestamp{0};
  unsigned long triggerId{0};
  uint64_t offset{0};

  uint32_t peakValue[N_DETECTORS][N_CHANNELS]{};

  template<typename... Args> void readTuple(const std::tuple<Args...>& t_) {
    isGood = std::get<0>(t_);
    eventSize = std::get<1>(t_);
    fwVersion = std::get<2>(t_);
    triggerNumber = std::get<3>(t_);
    boardId = std::get<4>(t_);
    timestamp = std::get<5>(t_);
    extTimestamp = std::get<6>(t_);
    triggerId = std::get<7>(t_);
    offset = std::get<8>(t_);
  }

};



#endif //BMRAWTOROOTCONVERTER_H
