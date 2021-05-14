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
      std::cout << "Trying to read file number " << val << std::endl;
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

int chk_evt_master_header(std::fstream &file, bool little_endian, int run_offset)
{
  bool is_good = false;
  unsigned char buffer[4];
  unsigned int val;
  int boards;

  if (!file.eof())
  {
    file.seekg(run_offset + 280);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

    if (val == 0x1234cccc || val == 0xcccc3412)
    {
      is_good = true;
    }
  }

  if (!is_good)
  {
    if (verbose)
    {
      std::cout << "ERROR: Can't find event master header for the event" << std::endl;
    }
    return -1;
  }

  return is_good;
}

std::tuple<int, int> chk_evt_RCD_header(std::fstream &file, bool little_endian, int run_offset)
{
  bool is_good = false;
  int boards = 0;
  int blank_evt_offset = 0;

  unsigned char buffer[4];
  unsigned int val;

  if (!file.eof())
  {
    file.seekg(run_offset + 292);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
    boards = val / 2644;

    file.seekg(run_offset + 296);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

    if (val == 0xee1234ee || val == 0xee3412ee)
    {
      is_good = true;

      file.seekg(run_offset + 296 + 92);
      file.read(reinterpret_cast<char *>(&buffer), 4);
      val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

      if (val == 0xee1234ee || val == 0xee3412ee)
      {
        if (verbose)
        {
          std::cout << "\nERROR: skipping blank event " << std::endl;
        }
        blank_evt_offset = 92;
      }
      file.seekg(run_offset + 296);
    }
  }

  if (!is_good)
  {
    if (verbose)
    {
      std::cout << "Can't find RCD header for the event" << std::endl;
    }
    return std::make_tuple(-1, -1);
  }

  return std::make_tuple(boards, blank_evt_offset);
}

bool read_evt_header(std::fstream &file, bool little_endian, int run_offset, int board)
{
  bool is_good = false;
  unsigned int evt_size = 0;

  unsigned char buffer[4];
  unsigned int val;

  if (!file.eof())
  {
    file.seekg(run_offset + board * 2644 + 348);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

    if (val == 0x34123412 || val == 0x12341234)
    {
      file.seekg(run_offset + board * 2644 + 356);
      file.read(reinterpret_cast<char *>(&buffer), 4);
      val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

      if (val == 0xbabadeea || val == 0xeadebaba)
      {
        is_good = true;
        return is_good;
      }
      else
      {
        if (verbose)
        {
          std::cout << "Can't find event builder header for the event" << std::endl;
        }
        return -1;
      }
    }
  }
}

std::vector<unsigned int> read_event(std::fstream &file, int offset, int board)
{

  file.seekg(offset + 396 + (board)*2644);

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
