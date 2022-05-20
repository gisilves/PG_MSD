#include "TMath.h"
#include "TROOT.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <tuple>
#include <unistd.h>
#include <iostream>

// for conversion with PAPERO_compress of FOOT PAPERO DAQ raw files to a rootfile with TTrees of raw events

int seek_run_header(std::fstream &file, unsigned int offset, bool verbose)
{
  unsigned int header;
  bool found = false;

  unsigned char buffer[4];
  unsigned int val;

  header = 0xcaf14afa;

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

std::tuple<bool, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, int> read_de10_header(std::fstream &file, unsigned int offset, bool verbose)
{
  unsigned char buffer[4];

  unsigned long evt_lenght = 0;
  unsigned long fw_version = 0;
  unsigned long trigger = 0;
  unsigned long board_id = 0;
  unsigned long trigger_id = 0;
  uint32_t timestamp_part = 0;
  uint64_t timestamp = 0UL;
  unsigned long val = 0;
  bool found = false;
  unsigned int original_offset = offset;

  unsigned long long header = 0xffffffffbaba1a9a;

  char dummy[100];

  if (verbose)
  {
    std::cout << "\t\nStarting from offset " << offset << std::endl;
  }

  file.seekg(offset);

  if (file.peek() != EOF)
  {
    while (!found && !file.eof())
    {
      file.seekg(offset + 4);
      file.read(reinterpret_cast<char *>(&buffer), 4);
      val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
      // std::cout << std::hex << val << std::endl;
      // sleep(1);

      if (val == header)
      {
        found = true;
        if (verbose)
        {
          std::cout << "Found DE10 header at offset " << offset << " with delta value of " << offset - original_offset << std::endl;
        }
      }
      else
      {
        offset += 4;
      }
    }
  }
  else
  {
    if (verbose)
    {
      std::cout << "Reached EOF" << std::endl;
    }
    return std::make_tuple(false, -1, -1, -1, -1, -1, -1, -1);
  }

  file.seekg(offset + 8);
  file.read(reinterpret_cast<char *>(&buffer), 4);
  val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
  evt_lenght = val - 10;

  file.read(reinterpret_cast<char *>(&buffer), 4);
  fw_version = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

  file.read(reinterpret_cast<char *>(&buffer), 4);
  trigger = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

  file.read(reinterpret_cast<char *>(&buffer), 4);
  board_id = buffer[2] | buffer[3] << 8;
  trigger_id = buffer[0] | buffer[1] << 8;

  file.read(reinterpret_cast<char *>(&buffer), 4);
  timestamp_part = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
  timestamp = 0x0000000000000000 | ((uint64_t)timestamp_part << 32UL);
  file.read(reinterpret_cast<char *>(&buffer), 4);
  timestamp_part = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
  timestamp |= (uint64_t)timestamp_part;

  if (verbose)
  {
    std::cout << "\t\tIn DE10Nano header: " << std::endl;
    std::cout << "\t\t\tevt_lenght: " << evt_lenght << std::endl;
    std::cout << "\t\t\tfw_version: " << std::hex << fw_version << std::endl;
    std::cout << "\t\t\ttrigger: " << trigger << std::endl;
    std::cout << "\t\t\tboard_id: " << board_id << std::endl;
    std::cout << "\t\t\ttrigger_id: " << trigger_id << std::endl;
  }

  return std::make_tuple(true, evt_lenght, fw_version, trigger, board_id, timestamp, trigger_id, offset);
}

std::vector<unsigned int> read_event(std::fstream &file, unsigned int offset, int event_size, bool verbose)
{

  file.seekg(offset + 40);
  if (verbose)
  {
    std::cout << "\tReading event at position " << offset + 40 << std::endl;
  }

  event_size = event_size * 2;

  unsigned char buffer[4];
  unsigned int val1;
  unsigned int val2;

  std::vector<unsigned int> event;

  for (size_t i = 0; i < event_size; i = i + 2)
  {
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val1 = buffer[0] | buffer[1] << 8;
    val2 = buffer[2] | buffer[3] << 8;

    event.push_back(val1 / 4);
    event.push_back(val2 / 4);
  }

  return event;
}
