#include "TMath.h"
#include "TROOT.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <unistd.h>
#include <iostream>

#define verbose false

int seek_endianess(std::fstream &file)
{
  bool found = false;
  bool little_endianess; //Expecting little endian file
  unsigned char buffer[2];
  unsigned short val;

  file.seekg(0); //Start of the binary file

  while (!found && !file.eof())
  {
    file.read(reinterpret_cast<char *>(&buffer), 2);
    val = buffer[0] | (buffer[1] << 8);

    if (val == 0xbbaa)
    {
      if (verbose)
      {
        std::cout << "WARNING: file is BIG Endian, part of the software expects LITTLE Endian" << std::endl;
      }
      little_endianess = false;
      found = true;
    }
    else if (val == 0xaabb)
    {
      if (verbose)
      {
        std::cout << "File is LITTLE Endian" << std::endl;
      }
      little_endianess = true;
      found = true;
    }
  }

  if (!found)
  {
    std::cout << "Can't find endianess WORD in the file" << std::endl;
    return -999;
  }
  else
  {
    return little_endianess;
  }
}

int seek_header(std::fstream &file, bool little_endian)
{
  int offset = 0;
  unsigned short header;
  bool found = false;

  unsigned char buffer[2];
  unsigned short val;

  if (little_endian)
  {
    header = 0xeb90;
  }
  else
  {
    header = 0x90eb;
  }

  while (!found && !file.eof())
  {
    file.seekg(offset);
    file.read(reinterpret_cast<char *>(&buffer), 2);
    val = buffer[0] | (buffer[1] << 8);

    if (val == header)
    {
      found = true;
    }
    else
    {
      offset += 2;
    }
  }

  if (!found)
  {
    std::cout << "Can't find event header in the file" << std::endl;
    return -999;
  }
  else
  {
    return offset;
  }
}

int seek_raw(std::fstream &file, int offset, bool little_endian)
{
  bool found = false;
  bool is_raw = 0;
  unsigned char buffer[2];
  unsigned short val;

  file.seekg(offset + 2);
  file.read(reinterpret_cast<char *>(&buffer), 2);

  if (little_endian)
  {
    val = (int)buffer[1];
  }
  else
  {
    val = (int)buffer[0];
  }

  if (val == 0xa0)
  {
    is_raw = true;
    found = true;
  }

  return is_raw;
}

int seek_version(std::fstream &file)
{
  bool found = false;
  int version;
  unsigned char buffer[2];
  unsigned short val;

  file.seekg(0);

  while (!found && !file.eof())
  {
    file.read(reinterpret_cast<char *>(&buffer), 2);
    val = buffer[0] | (buffer[1] << 8);

    if (val == 0x1212 || val == 0x1313)
    {
      version = val;
      found = true;
    }
  }

  if (!found)
  {
    std::cout << "Can't find version WORD in the file" << std::endl;
    return -999;
  }
  else
  {
    return version;
  }
}

std::vector<unsigned short> read_event(std::fstream &file, int offset,
                                       int version, int evt)
{
  int bitsize = -999;

  if (version == 0x1212)
  {
    bitsize = 1024;
  }
  else
  {
    bitsize = 2048;
  }

  file.seekg(offset + 4 + evt * bitsize);

  int event_size;

  if (version == 0x1212)
  {
    event_size = 384;
  }
  else if (version == 0x1313)
  {
    event_size = 640;
  }
  else
  {
    std::cout << "Error: unknown miniTRB version" << std::endl;
  }

  std::vector<unsigned short> buffer(event_size);
  file.read(reinterpret_cast<char *>(buffer.data()), buffer.size() * 2);
  std::streamsize s = file.gcount();
  buffer.resize(s / 2);

  std::vector<unsigned short> event(s / 2);

  int j = 0;
  for (int i = 0; i < buffer.size(); i += 2)
  {
    event.at(j) = buffer.at(i);
    event.at(j + buffer.size() / 2) = buffer.at(i + 1);
    j++;
  }

  return event;
}
