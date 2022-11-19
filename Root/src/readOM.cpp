#include "readOM.h"
#include <iostream>
#include <fstream>
#include <string>

#include <chrono>

template <typename T>
std::vector<T> reorder(std::vector<T> const &v)
{
  std::vector<T> reordered_vec(v.size());
  int j = 0;
  constexpr int order[] = {1, 0, 3, 2, 5, 4, 7, 6, 9, 8};
  for (int ch = 0; ch < 128; ch++)
  {
    for (int adc : order)
    {
      reordered_vec.at(adc * 128 + ch) = v.at(j);
      j++;
    }
  }
  return reordered_vec;
}


readOM::readOM()
{
    omServer = new udpServer(kUdpAddr, kUdpPort);
}

void readOM::DoGetUDP()
{
  uint32_t header;
  omServer->Rx(&header, sizeof(header));
  std::cout << "Header: " << header << std::endl;

  if (header != 0xfa4af1ca)
  {
    std::cout << "ERROR: header is not correct, skipping packet" << std::endl;
    return;
  }

  uint32_t word1;
  omServer->Rx(&word1, sizeof(word1));
    std::cout << "Word1: " << word1 << std::endl;

  uint32_t word2;
  omServer->Rx(&word2, sizeof(word2));
    std::cout << "Word2: " << word2 << std::endl;

  //should be number of detectors connected
  uint16_t word3;
  omServer->Rx(&word3, sizeof(word3));
    std::cout << "Word3: " << word3 << std::endl;

  uint16_t word4;
  omServer->Rx(&word4, sizeof(word4));
    std::cout << "Word4: " << word4 << std::endl;

  omServer->Rx(evt.data(), 2600);

  for (size_t i = 0; i < evt.size() - 10; i++)
  {
    evt_buffer.push_back((evt.at(i + 9) % (0x10000)) / 4);
    evt_buffer.push_back(((evt.at(i + 9) >> 16) % (0x10000)) / 4);
  }

  evt_buffer = reorder(evt_buffer);
  detJ5 = std::vector<uint32_t>(evt_buffer.begin(), evt_buffer.begin() + evt_buffer.size() / 2);
  detJ7 = std::vector<uint32_t>(evt_buffer.begin() + evt_buffer.size() / 2, evt_buffer.end());

    std::cout << "detJ5: " << detJ5.size() << std::endl;
    std::cout << "detJ7: " << detJ7.size() << std::endl;

}

readOM::~readOM()
{
  delete omServer;
}

int main()
{
  readOM om;
  while (true)
  {
    om.DoGetUDP();
  }
}