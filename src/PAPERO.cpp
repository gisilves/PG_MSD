#include "TMath.h"
#include "TROOT.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <tuple>
#include <unistd.h>
#include <iostream>
#include <time.h>
#include "PAPERO.h"

bool seek_file_header(std::fstream &file, uint32_t offset, int verbose)
{
  uint32_t file_known_word = 0xB01ADEEE;
  bool found = false;

  unsigned char buffer[4];
  uint32_t val;

  file.seekg(offset);
  file.read(reinterpret_cast<char *>(&buffer), 4);
  val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

  if (val == file_known_word)
  {
    found = true;
    if (verbose == 1)
    {
      std::cout << "Found file header at offset " << offset << std::endl;
    }
  }

  if (!found)
  {
    if (verbose == 1)
    {
      std::cout << "Can't find file header in the file" << std::endl;
    }
    return false;
  }
  else
  {
    return true;
  }
}

std::tuple<bool, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, std::vector<uint16_t>, uint32_t> read_file_header(std::fstream &file, uint32_t offset, int verbose)
{
  unsigned char buffer[4];
  uint32_t unix_time;
  uint32_t maka_hash;
  uint16_t type;
  uint16_t version;
  uint16_t n_detectors;
  std::vector<uint16_t> detector_ids;

  file.seekg(offset + 4);
  file.read(reinterpret_cast<char *>(&buffer), 4);
  unix_time = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
  // convert unix time to date
  time_t rawtime = unix_time;
  struct tm *timeinfo;
  timeinfo = localtime(&rawtime);
  char date[80];
  strftime(date, 80, "%Y-%m-%d %H:%M:%S", timeinfo);

  file.read(reinterpret_cast<char *>(&buffer), 4);
  maka_hash = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

  file.read(reinterpret_cast<char *>(&buffer), 2);
  n_detectors = buffer[0] | buffer[1] << 8;

  file.read(reinterpret_cast<char *>(&buffer), 2);
  version = ((buffer[0] | buffer[1] << 8) & 0x0FFF);

  type = ((buffer[0] | buffer[1] << 8) & 0xF000) >> 12;

  if (verbose == 1)
  {
    std::cout << "\nFile header: " << std::endl;
    std::cout << "\tunix_time: " << unix_time << std::endl;
    std::cout << "\t\tdate: " << date << std::endl;
    std::cout << "\tmaka_hash: " << maka_hash << std::endl;
    std::cout << "\tn_detectors: " << n_detectors << std::endl;
    std::cout << "\tversion: " << std::hex << version << std::endl;
    std::cout << "\ttype: " << type << std::endl;
  }

  for (int i = 0; i < n_detectors; i++)
  {
    file.read(reinterpret_cast<char *>(&buffer), 2);
    detector_ids.push_back(buffer[0] | buffer[1] << 8);
  }

  if (n_detectors % 2)
  {
    // move 2 bits ahead in file
    file.seekg(2, std::ios_base::cur);
  }

  return std::make_tuple(true, unix_time, maka_hash, type, version, n_detectors, detector_ids, file.tellg());
}

int seek_first_evt_header(std::fstream &file, uint32_t offset, int verbose)
{
  uint32_t header;
  bool found = false;

  unsigned char buffer[4];
  uint32_t val;

  header = 0xcaf14afa;

  while (!found && !file.eof())
  {
    file.seekg(offset);
    file.read(reinterpret_cast<char *>(&buffer), 4);
    val = buffer[3] | buffer[2] << 8 | buffer[1] << 16 | buffer[0] << 24;

    if (val == header)
    {
      found = true;
      if (verbose == 1)
      {
        std::cout << "Found maka header at offset " << offset << std::endl;
      }
    }
    else
    {
      offset += 4;
    }
  }

  if (!found)
  {
    if (verbose == 1)
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

std::tuple<bool, uint32_t, uint32_t, uint16_t, uint16_t, uint16_t, uint32_t> read_evt_header(std::fstream &file, uint32_t offset, int verbose)
{
  uint32_t header;
  unsigned char buffer[4];
  uint32_t val;
  uint32_t lenght_in_bytes;
  uint32_t evt_number;
  uint16_t n_detectors;
  uint16_t status;
  uint16_t type;

  header = 0xcaf14afa;

  file.seekg(offset);
  file.read(reinterpret_cast<char *>(&buffer), 4);
  val = buffer[3] | buffer[2] << 8 | buffer[1] << 16 | buffer[0] << 24;

  if (val == header)
  {
    if (verbose == 1)
    {
      std::cout << "Evt header is present " << std::endl;
    }
  }
  else
  {
    if (verbose == 1)
    {
      std::cout << "Can't find event header"
                << " at offset " << offset << std::endl;
    }
    return std::make_tuple(false, -1, -1, -1, -1, -1, -1);
  }
  
  file.read(reinterpret_cast<char *>(&buffer), 4);
  lenght_in_bytes = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

  file.read(reinterpret_cast<char *>(&buffer), 4);
  evt_number = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

  file.read(reinterpret_cast<char *>(&buffer), 2);
  n_detectors = buffer[0] | buffer[1] << 8;

  file.read(reinterpret_cast<char *>(&buffer), 2);
  status = ((buffer[0] | buffer[1] << 8) & 0x0FFF);
  type = ((buffer[0] | buffer[1] << 8) & 0xF000) >> 12;

  if (verbose == 1)
  {
    std::cout << "MAKA header: " << std::endl;
    std::cout << "\tlenght_in_bytes: " << std::dec << lenght_in_bytes << std::endl;
    std::cout << "\tevt_number: " << evt_number << std::endl;
    std::cout << "\tn_detectors: " << n_detectors << std::endl;
    std::cout << "\tstatus: " << status << std::endl;
    std::cout << "\ttype: " << type << std::endl;
  }

  return std::make_tuple(true, lenght_in_bytes, evt_number, n_detectors, status, type, file.tellg());
}

bool read_old_evt_header(std::fstream &file, uint32_t offset, int verbose)
{
  uint32_t header;
  unsigned char buffer[4];
  uint32_t val;

  header = 0xcaf14afa;

  file.seekg(offset);
  file.read(reinterpret_cast<char *>(&buffer), 4);
  val = buffer[3] | buffer[2] << 8 | buffer[1] << 16 | buffer[0] << 24;

  if (val == header)
  {
    if (verbose == 1)
    {
      std::cout << "Evt header is present " << std::endl;
    }
    return true;
  }
  else
  {
    if (verbose == 1)
    {
      std::cout << "Can't find event header"
                << " at offset " << offset << std::endl;
    }
    return false;
  }
}

bool read_de10_footer(std::fstream &file, uint32_t offset, int verbose)
{
  uint32_t footer;
  unsigned char buffer[4];
  uint32_t val;

  footer = 0xcefaed0b;

  file.seekg(offset);
  file.read(reinterpret_cast<char *>(&buffer), 4);
  val = buffer[3] | buffer[2] << 8 | buffer[1] << 16 | buffer[0] << 24;

  if (val == footer)
  {
    if (verbose == 1)
    {
      std::cout << "DE10 footer is present " << std::endl;
    }
    return true;
  }
  else
  {
    if (verbose == 1)
    {
      std::cout << "Can't find DE10 footer" << std::endl;
    }
    return false;
  }
}

std::tuple<bool, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint64_t, uint32_t, int> read_de10_header(std::fstream &file, uint32_t offset, int verbose)
{
  unsigned char buffer[4];

  uint32_t evt_lenght = 0;
  uint32_t fw_version = 0;
  uint32_t trigger = 0;
  uint32_t board_id = 0;
  uint32_t trigger_id = 0;
  uint32_t i2cmsg_part = 0;
  uint64_t i2cmsg = 0UL;
  uint32_t ext_timestamp_part = 0;
  uint64_t ext_timestamp = 0UL;
  uint32_t val = 0;
  bool found = false;
  uint32_t original_offset = offset;

  uint32_t header = 0xbaba1a9a;

  char dummy[100];

  if (verbose == 1)
  {
    std::cout << "\t\nStarting from offset " << offset << std::endl;
  }

  file.seekg(offset);

  if (file.peek() != EOF)
  {
    while (!found && !file.eof())
    {
      file.seekg(offset);
      file.read(reinterpret_cast<char *>(&buffer), 4);
      val = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;

      if (val == header)
      {
        found = true;
        if (verbose == 1)
        {
          std::cout << "Found DE10 header at offset " << offset << " with delta value of " << offset - original_offset << std::endl;
        }
      }
      else
      {
        offset += 4;
      }
    }
    if (!found)
    {
      std::cout << "\n\tCan't find DE10 header, closing file ..." << std::endl;
      return std::make_tuple(false, -1, -1, -1, -1, -1, -1, -1, -1);
    }
  }
  else
  {
    if (verbose == 1)
    {
      std::cout << "Reached EOF" << std::endl;
    }
    return std::make_tuple(false, -1, -1, -1, -1, -1, -1, -1, -1);
  }

  // file.seekg(offset + 8);
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
  i2cmsg_part = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
  i2cmsg = 0x0000000000000000 | ((uint64_t)i2cmsg_part << 32UL);
  file.read(reinterpret_cast<char *>(&buffer), 4);
  i2cmsg_part = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
  i2cmsg |= (uint64_t)i2cmsg_part;

  file.read(reinterpret_cast<char *>(&buffer), 4);
  ext_timestamp_part = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
  ext_timestamp = 0x0000000000000000 | ((uint64_t)ext_timestamp_part << 32UL);

  file.read(reinterpret_cast<char *>(&buffer), 4);
  ext_timestamp_part = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
  ext_timestamp |= (uint64_t)ext_timestamp_part;

  if (verbose == 1)
  {
    std::cout << "\t\tIn DE10Nano header: " << std::endl;
    std::cout << "\t\t\tevt_lenght: " << evt_lenght << std::endl;
    std::cout << "\t\t\tfw_version: " << std::hex << fw_version << std::endl;
    std::cout << "\t\t\ttrigger: " << std::dec << trigger << std::endl;
    std::cout << "\t\t\tboard_id: " << board_id << std::endl;
    std::cout << "\t\t\ttrigger_id: " << trigger_id << std::endl;

    std::cout << "\t\t\texternal timestamp: " << std::dec << ext_timestamp << std::endl;
  }


  return std::make_tuple(true, evt_lenght, fw_version, trigger, board_id, i2cmsg, ext_timestamp, trigger_id, offset);
}

std::vector<uint32_t> read_eventHEF(std::fstream &file, uint32_t offset, int event_size, int verbose)
{

  file.seekg(offset + 36);
  if (verbose == 1)
  {
    std::cout << "\tReading event at position " << offset + 36 << std::endl;
  }

  event_size = event_size * 2;

  unsigned char buffer[4];
  uint32_t val1;
  uint32_t val2;

  std::vector<uint32_t> event;

  for (size_t i = 0; i < event_size; i = i + 2)
  {
    file.read(reinterpret_cast<char *>(&buffer), 4);

    val1 = buffer[0] | buffer[1] << 8;
    val2 = buffer[2] | buffer[3] << 8;

    // Remove padding (2 bits)
    val1 = val1 & 0xfffffffc;
    val2 = val2 & 0xfffffffc;

    event.push_back(val1);
    event.push_back(val2);  
  }

  return event;
}
