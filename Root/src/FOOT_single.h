#include "TMath.h"
#include "TROOT.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <unistd.h>
#include <iostream>

#define verbose false

int seek_header(std::fstream &file, bool little_endian)
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
    }
    else
    {
      offset += 4;
    }
  }

  if (!found)
  {
    std::cout << "Can't find event header in the file" << std::endl;
    return -999;
  }
  else
  {
    return offset+264;
  }
}


bool chk_evt_master_header(std::fstream &file, bool little_endian, int run_offset)
{
  bool is_good = false;
  int master_offset = run_offset + 84;

  unsigned char buffer[4];
  unsigned int val;

  if (!file.eof())
  {
    file.seekg(master_offset);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    
    val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

    if (val == 0x12341234 || val == 0x34123412)
    {
      is_good = true;
    }
  }

  if (!is_good)
  {
    std::cout << "Can't find event master header for the event" << std::endl;
    return is_good;
  }

  return is_good;
}

bool read_evt_builder_header(std::fstream &file, bool little_endian, int run_offset)
{
  bool is_good = false;
  int builder_offset = run_offset + 92;

  unsigned char buffer[4];
  unsigned int val;

  if (!file.eof())
  {
    file.seekg(builder_offset);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    
    val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

    if (val == 0xbabadeea || val == 0xeadebaba)
    {
      is_good = true;
    }
  }

  if (!is_good)
  {
    std::cout << "Can't find event builder header for the event" << std::endl;
    return is_good;
  }

  return is_good;
}


unsigned int read_evt_header(std::fstream &file, bool little_endian, int run_offset)
{
  bool is_good = false;
  int evt_offset = run_offset + 116;
  unsigned int evt_size = 0;

  unsigned char buffer[4];
  unsigned int val;

  if (!file.eof())
  {
    file.seekg(evt_offset);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

    if (val == 0x0105ad4e || val == 0x4ead0501)
    {
      is_good = true;
      file.seekg(run_offset + 112);
      file.read(reinterpret_cast<char *>(&buffer), 4);
      evt_size = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
    }
  }

  if (!is_good)
  {
    std::cout << "Can't find event builder header for the event" << std::endl;
    return -1;
  }

  return evt_size;
}

std::vector<unsigned int> read_event(std::fstream &file, int offset, int evt)
{
  int bitsize = 2712;

  file.seekg(offset + 132 + evt * bitsize);

  int event_size = 1280;

  unsigned char buffer[4];
  unsigned int val1;
  unsigned int val2;
  char test[10];

  std::vector<unsigned int> event(event_size);

  for (int i = 0; i < event.size(); i=i+2)
  {
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val1 = buffer[0] | buffer[1] << 8;
    val2 = buffer[2] | buffer[3] << 8;

    event.at(i) = val1/4;
    event.at(i+1) = val2/4;
  }

  return event;
}
