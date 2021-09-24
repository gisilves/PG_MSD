#include "TMath.h"
#include "TROOT.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <tuple>
#include <unistd.h>
#include <iostream>

#define verbose false

//for conversion with FOOT_compress of FOOT DAQ raw files to a rootfile with TTrees of raw events

int seek_run_header(std::fstream &file)
{
  int offset = 0;
  unsigned int header;
  bool found = false;

  unsigned char buffer[4];
  unsigned int val;

  header = 0xbaba1afa;

  while (!found && !file.eof())
  {
    file.seekg(offset);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val = buffer[3] | buffer[2] << 8 | buffer[1] << 16 | buffer[0] << 24;

    if (val == header)
    {
      found = true;
      if (verbose)
      {
        std::cout << "Found header at offset " << offset << std::endl;
      }
    }
    else
    {
      offset += 4;
    }
  }

  if (!found)
  {
    if (verbose)
    {
      std::cout << "Can't find event header in the file" << std::endl;
    }
    return -999;
  }
  else
  {
    return offset;
  }
}

std::tuple<bool, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long> read_evt_header(std::fstream &file, int offset)
{
  unsigned char buffer[4];

  unsigned long evt_lenght = 0;
  unsigned long fw_version = -1;
  unsigned long trigger = 0;
  unsigned long board_id = -1;
  unsigned long timestamp = -1;
  unsigned long val;

  if (verbose)
  {
    std::cout << "\t\nStarting from offset " << offset << std::endl;
  }

  file.seekg(offset + 4);

  if (file.peek() != EOF)
  {
    file.seekg(offset + 4);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val = buffer[3] | buffer[2] << 8 | buffer[1] << 16 | buffer[0] << 24;
    evt_lenght = val - 10;

    file.read(reinterpret_cast<char *>(&buffer), 4);
    fw_version = buffer[3] | buffer[2] << 8 | buffer[1] << 16 | buffer[0] << 24;

    file.read(reinterpret_cast<char *>(&buffer), 4);
    trigger = buffer[3] | buffer[2] << 8 | buffer[1] << 16 | buffer[0] << 24;

    file.read(reinterpret_cast<char *>(&buffer), 4);
    board_id = buffer[1];

    file.read(reinterpret_cast<char *>(&buffer), 4);
    timestamp = buffer[3] | buffer[2] << 8 | buffer[1] << 16 | buffer[0] << 24;

    return std::make_tuple(true, evt_lenght, fw_version, trigger, board_id, timestamp);
  }
  else
  {
    if (verbose)
    {
      std::cout << "Reached EOF" << std::endl;
    }
    return std::make_tuple(false, -1, -1, -1, -1, -1);
  }
}

std::vector<unsigned int> read_event(std::fstream &file, int offset, int event_size)
{

  file.seekg(offset + 36);
  if (verbose)
  {
    std::cout << "\tReading event at position " << offset + 36 << std::endl;
  }

  event_size = event_size * 2;

  unsigned char buffer[4];
  unsigned int val1;
  unsigned int val2;

  std::vector<unsigned int> event(event_size);

  for (int i = 0; i < event.size(); i = i + 2)
  {
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val1 = buffer[3] | buffer[2] << 8;
    val2 = buffer[1] | buffer[0] << 8;

    event.at(i) = val1 / 4;
    event.at(i + 1) = val2 / 4;
  }

  return event;
}
