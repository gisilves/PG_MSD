#include "TMath.h"
#include "TROOT.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <tuple>
#include <unistd.h>
#include <iostream>

// for conversion with AMS_compress of AMS L0 DAQ raw files to a rootfile with TTrees of raw events
// Imported from https://sourceforge.net/projects/amsdaq/

int ReadFile(void *ptr, size_t size, size_t nitems, FILE *stream)
{

  int ret = 0;
  ret = fread(ptr, size, nitems, stream);
  if (feof(stream))
  {
    printf("\n");
    printf("End of File \n");
    return -1;
  }
  if (ferror(stream))
  {
    printf("Error reading \n");
    return -2;
  }

  return ret;
}

unsigned short int bit(int bitno, unsigned short int data)
{

  return (data & 1 << bitno) >> bitno;
}

unsigned short int bit(int bitnofirst, int bitnolast, unsigned short int data)
{

  unsigned short int mask = 0;
  for (int bb = bitnofirst; bb <= bitnolast; bb++)
  {
    mask |= (1 << bb);
  }

  return (data & mask) >> bitnofirst;
}


void ReOrderVladimir(std::vector<unsigned char> &data, std::vector<unsigned short> &data_ord)
{

  std::vector<unsigned short> data_ord_tmp;
  data_ord_tmp.resize(((int)((8.0 / 14.0) * ((int)(data.size())))));

  data_ord.resize(((int)((8.0 / 14.0) * ((int)(data.size())))));

  int ch_in_2va = -1;

  for (int byte = 0; byte < ((int)data.size()); byte += 14)
  {
    ch_in_2va++;
    for (int bit_nec = 0; bit_nec < 14; bit_nec++)
    {
      int bit = bit_nec;
      for (int adc = 0; adc < 8; adc++)
      {
        int ch = adc * 128 + ch_in_2va;
        data_ord_tmp[ch] |= ((data[byte + bit_nec] & (1 << adc)) >> adc) << bit;
      }
    }
  }

  for (int ch = 0; ch < ((int)data_ord_tmp.size()); ch++)
  {
    int va = ch / 64;
    int ch_in_va = ch % 64;
    int new_ch = (16 - va - 1) * 64 + ch_in_va;
    data_ord[new_ch] = data_ord_tmp[ch];
  }
  return;
}

int ProcessBlock(FILE *file, unsigned int &size_consumed, int &ev_found, std::vector<std::vector<unsigned short>> &signals_by_ev, int nesting_level)
{

  int fstat = 0;
  unsigned int size_to_read;
  size_consumed = 0;
  unsigned short int dummy;

  unsigned short int size;
  unsigned short int size_ext;
  unsigned int size_full;

  unsigned short int na;

  unsigned short int dt;
  unsigned short int dt_ext;
  unsigned int dt_full;

  fstat = ReadFile(&size, sizeof(size), 1, file);
  size_consumed += sizeof(size);
  if (fstat == -1)
    return 1;

  if (bit(15, size))
  {
    fstat = ReadFile(&size_ext, sizeof(size_ext), 1, file);
    size_consumed += sizeof(size_ext);
    if (fstat == -1)
      return 1;
    size &= 0x7fff; // mask the first bit
    size_full = (size << 16) + size_ext;
  }
  else
  {
    size &= 0x7fff; // mask the first bit
    size_full = size;
  }

  size_to_read = size_full;

  fstat = ReadFile(&dummy, sizeof(dummy), 1, file);
  size_consumed += sizeof(dummy);
  size_to_read -= sizeof(dummy);
  if (fstat == -1)
    return 1;

  na = bit(5, 13, dummy); // from bit 5 to bit 13
  dt = bit(0, 4, dummy);  // from bit 0 to bit 4

  if (dt == 0x1f)
  {
    fstat = ReadFile(&dt_ext, sizeof(dt_ext), 1, file);
    size_consumed += sizeof(dt_ext);
    size_to_read -= sizeof(dt_ext);
    if (fstat == -1)
      return 1;
    dt_full = (dt << 16) + dt_ext;
  }
  else
  {
    dt_full = dt;
  }

  // other cases in the file are:
  // dt_full == 0x1f0205
  // dt_full == 0x4

  if (dt_full == 0x1f0383)
  { // fine time envelope
    fstat = ReadFile(&dummy, sizeof(dummy), 1, file);
    size_consumed += sizeof(dummy);
    size_to_read -= sizeof(dummy);
    if (fstat == -1)
      return 1;

    unsigned short int status = bit(12, 15, dummy); // from bit 12 to bit 15
    unsigned short int tag = bit(0, 11, dummy);     // from bit 0 to bit 11

    fstat = ReadFile(&dummy, sizeof(dummy), 1, file);
    size_consumed += sizeof(dummy);
    size_to_read -= sizeof(dummy);
    if (fstat == -1)
      return 1;

    unsigned short int utime_sec_msb = bit(0, 15, dummy);

    fstat = ReadFile(&dummy, sizeof(dummy), 1, file);
    size_consumed += sizeof(dummy);
    size_to_read -= sizeof(dummy);
    if (fstat == -1)
      return 1;

    unsigned short int utime_sec_lsb = bit(0, 15, dummy);
    unsigned int utime_sec = (utime_sec_msb << 16) + utime_sec_lsb;

    fstat = ReadFile(&dummy, sizeof(dummy), 1, file);
    size_consumed += sizeof(dummy);
    size_to_read -= sizeof(dummy);
    if (fstat == -1)
      return 1;

    unsigned short int utime_usec_msb = bit(0, 15, dummy);

    fstat = ReadFile(&dummy, sizeof(dummy), 1, file);
    size_consumed += sizeof(dummy);
    size_to_read -= sizeof(dummy);
    if (fstat == -1)
      return 1;

    unsigned short int utime_usec_lsb = bit(0, 15, dummy);

    unsigned int utime_usec = (utime_usec_msb << 16) + utime_usec_lsb;

    unsigned int read_bytes = 0;
    int ret = ProcessBlock(file, read_bytes, ev_found, signals_by_ev, nesting_level + 1);
    if (ret != 0)
      return ret;
    size_consumed += read_bytes;
    size_to_read -= read_bytes;
  }
  else if (dt_full == 0x13)
  { // SCI/CAL/CFG/HK/ (0x5, 0x6, 0x7, 0x8) + LVL3 + GPS data
    fstat = ReadFile(&dummy, sizeof(dummy), 1, file);
    size_consumed += sizeof(dummy);
    size_to_read -= sizeof(dummy);
    if (fstat == -1)
      return 1;

    unsigned short int status = bit(12, 15, dummy); // from bit 12 to bit 15

    unsigned short int tag = bit(0, 11, dummy); // from bit 0 to bit 11

    fstat = ReadFile(&dummy, sizeof(dummy), 1, file);
    size_consumed += sizeof(dummy);
    size_to_read -= sizeof(dummy);
    if (fstat == -1)
      return 1;

    unsigned short int utime_sec_msb = bit(0, 15, dummy);

    fstat = ReadFile(&dummy, sizeof(dummy), 1, file);
    size_consumed += sizeof(dummy);
    size_to_read -= sizeof(dummy);
    if (fstat == -1)
      return 1;

    unsigned short int utime_sec_lsb = bit(0, 15, dummy);

    unsigned int utime_sec = (utime_sec_msb << 16) + utime_sec_lsb;

    if (size_to_read == 0)
    {
      printf("Empty event...\n");
    }
    else if (size_to_read == 1794)
    { // Vladimir raw event
      fstat = ReadFile(&dummy, sizeof(dummy), 1, file);
      size_consumed += sizeof(dummy);
      size_to_read -= sizeof(dummy);
      if (fstat == -1)
        return 1;

      unsigned short int evtn = bit(0, 7, dummy); // from bit 0 to bit 7

      std::vector<unsigned char> data_end_nc;
      std::vector<unsigned char> data;
      //      printf("data size (FEP): %d\n", size_to_read);
      data_end_nc.resize(size_to_read);
      data.resize(size_to_read);

      fstat = ReadFile(&data_end_nc[0], size_to_read, 1, file);
      size_consumed += size_to_read;
      size_to_read -= size_to_read;
      if (fstat == -1)
        return 1;

      // Endianess correction
      for (int ii = 0; ii < ((int)(data_end_nc.size())); ii += 2)
      {
        data[ii] = data_end_nc[ii + 1];
        data[ii + 1] = data_end_nc[ii];
      }

      std::vector<unsigned short> data_ord;
      ReOrderVladimir(data, data_ord);
      signals_by_ev.push_back(data_ord);

      ev_found++;
    }
    else
    {
      printf("Wrong raw event size (%d): it should be 0 or 1794...\n", size_to_read);
    }
  }

  // skip all the block data
  if (int ret0 = fseek(file, size_to_read, SEEK_CUR))
  {
    printf("Fatal: error during file skip ret0=%d \n", ret0);
    return -99;
  }
  size_consumed += size_to_read;

  return 0;
}