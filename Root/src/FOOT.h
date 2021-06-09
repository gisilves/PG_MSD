#include "TMath.h"
#include "TROOT.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <tuple>
#include <unistd.h>
#include <iostream>

#define verbose false

int seek_run_header(std::fstream &file, bool little_endian)
{
  int offset = 0;
  unsigned int header;
  bool found = false;

  unsigned char buffer[4];
  unsigned int val;

  if (little_endian)
  {
    header = 0xaaaa3412;
  }
  else
  {
    header = 0x1234aaaa;
  }

  while (!found && !file.eof())
  {
    file.seekg(offset);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

    if (val == header)
    {
      found = true;
      if (verbose)
      {
        std::cout << "Found header at offset " << offset << std::endl;
      }
      file.seekg(offset + 240);
      file.read(reinterpret_cast<char *>(&buffer), 4);
      val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
      if (verbose)
      {
        std::cout << "Trying to read file number " << val << std::endl;
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
std::tuple<bool, int> chk_evt_master_header(std::fstream &file, bool little_endian, int run_offset)
{
  unsigned char buffer[4];
  unsigned int val;
  int boards;
  bool found = false;

  while (!file.eof() && !found)
  {
    file.seekg(run_offset);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

    if (val == 0x1234cccc || val == 0xcccc3412)
    {
      if (verbose)
      {
        std::cout << "Found event master header at position " << run_offset << std::endl;
      }
      found = true;
      return std::make_tuple(true, run_offset);
    }
    else
    {
      run_offset += 4;
    }
  }

  if (!found)
  {
    if (verbose)
    {
      std::cout << "ERROR: Can't find event master header for the event" << std::endl;
    }
    return std::make_tuple(false, run_offset);
  }
}

std::tuple<int, int> chk_evt_RCD_header(std::fstream &file, bool little_endian, int run_offset)
{
  bool is_good = false;
  int boards = 0;
  int RCD_offset = 0;
  bool found = false;

  unsigned char buffer[4];
  unsigned int val;

  while (!file.eof() && !found)
  {
    file.seekg(run_offset);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

    if (val == 0xee1234ee || val == 0xee3412ee)
    {
      is_good = true;
      found = true;

      file.seekg(run_offset - 4);
      file.read(reinterpret_cast<char *>(&buffer), 4);
      val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
      boards = val / 2644;

      file.seekg(run_offset + 92);
      file.read(reinterpret_cast<char *>(&buffer), 4);
      val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

      if (val == 0xee1234ee || val == 0xee3412ee)
      {
        if (verbose)
        {
          std::cout << "\nERROR: skipping blank event " << std::endl;
        }
        run_offset += 92;
      }
    }
    else
    {
      run_offset += 4;
    }
  }

  if (verbose)
  {
    std::cout << "Found RCD header at position " << run_offset << std::endl;
  }

  if (!is_good)
  {
    if (verbose)
    {
      std::cout << "Can't find RCD header for the event" << std::endl;
    }
    return std::make_tuple(-1, -1);
  }

  return std::make_tuple(boards, run_offset);
}

std::tuple<bool, unsigned short, int> read_evt_header(std::fstream &file, bool little_endian, int run_offset, int board)
{
  bool found = false;
  unsigned int evt_size = 0;
  unsigned short timestamp = 0;
  unsigned char buffer[4];
  unsigned int val;

  if (verbose)
  {
    std::cout << "\n\nStarting from offset " << run_offset << std::endl;
  }

  while (!file.eof() & !found)
  {
    file.seekg(run_offset);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

    if (val == 0x1234cccc || val == 0xcccc3412)
    {
      if (verbose)
      {
        std::cout << "ERROR: board number " << board << " data not found, skipping to next event" << std::endl;
      }
      return std::make_tuple(false, timestamp, run_offset);
    }

    if (val == 0x34123412 || val == 0x12341234)
    {
      file.seekg(run_offset + 8);
      file.read(reinterpret_cast<char *>(&buffer), 4);
      val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
      
      if (val == 0xbabadeea || val == 0xeadebaba)
      {
        file.seekg(run_offset - 12);
        file.read(reinterpret_cast<char *>(&buffer), 4);
        timestamp = buffer[0] | buffer[1] << 8;
        found = true;

        if (verbose)
        {
          std::cout << "Found evt header at position " << run_offset + 8 << std::endl;
        }

        return std::make_tuple(true, timestamp, run_offset + 8);
        break;
      }
      else
      {
        if (verbose)
        {
          std::cout << "Can't find event builder header for the event" << std::endl;
        }
        return std::make_tuple(false, -1, run_offset + 8);
      }
    }
    else
    {
      run_offset += 4;
    }
  }
}

std::vector<unsigned int> read_event(std::fstream &file, int offset, int board)
{

  file.seekg(offset + 40);
  if (verbose)
  {
    std::cout << "Reading event at position " << offset + 40 << std::endl;
  }

  int event_size = 1280;

  unsigned char buffer[4];
  unsigned int val1;
  unsigned int val2;
  char test[10];

  std::vector<unsigned int> event(event_size);

  for (int i = 0; i < event.size(); i = i + 2)
  {
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val1 = buffer[0] | buffer[1] << 8;
    val2 = buffer[2] | buffer[3] << 8;

    event.at(i) = val1 / 4;
    event.at(i + 1) = val2 / 4;
  }

  return event;
}
