#include <dirent.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include <termios.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>
#include <sys/sysinfo.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <byteswap.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define NIPMREG_HVSTATUS 0
#define NIPMREG_FBMODE 1
#define NIPMREG_VTARGET 2
#define NIPMREG_RAMP 3
#define NIPMREG_MAXV 4
#define NIPMREG_MAXI 5
#define NIPMREG_MAXT 6
#define NIPMREG_TEMP_M2 7
#define NIPMREG_TEMP_M 8
#define NIPMREG_TEMP_Q 9
#define NIPMREG_ALFA_V 10
#define NIPMREG_ALFA_I 11
#define NIPMREG_ALFA_VREF 12
#define NIPMREG_ALFA_TREF 13
#define NIPMREG_TCOEF 28
#define NIPMREG_LUTENABLE 29
#define NIPMREG_PIDENABLE 30
#define NIPMREG_EMERGENCYSTOP 31
#define NIPMREG_IZERO 33
#define NIPMREG_LUTADDRESS 36
#define NIPMREG_LUTT 37
#define NIPMREG_LUTV 38
#define NIPMREG_POINTn 39
#define NIPMREG_IIC_BA 40

#define NIPMREG_UNDERVOLTAGE 227
#define NIPMREG_NTCTEMP 228
#define NIPMREG_DIGITALIO 229
#define NIPMREG_VIN 230
#define NIPMREG_VOUT 231
#define NIPMREG_IOUT 232
#define NIPMREG_VREF 233
#define NIPMREG_TREF 234
#define NIPMREG_VTARGET_R 235
#define NIPMREG_RTARGET_R 236
#define NIPMREG_CORRECTIONVOLTAGE 237
#define NIPMREG_PIDOUT 238
#define NIPMREG_COMPV 249
#define NIPMREG_COMPI 250

#define NIPMREG_PDCODE 251
#define NIPMREG_FWVER 252
#define NIPMREG_HWVER 253
#define NIPMREG_SN 254
#define NIPMREG_WEEPROM 255

enum HVFeedbackMode
{
    HVFB_digital = 0,
    HVFB_analog = 1,
    HVFB_temperature = 2
};

void SetNIPMRegFloat(int dev, int device_address, uint8_t register_n, float data);
int SetNIPMRegBoolean(int dev, int device_address, uint8_t register_n, bool data);
void SetNIPMRegInteger(int dev, int device_address, uint8_t register_n, int32_t data);
int GetNIPMRegFloat(int dev, int device_address, uint8_t register_n, float *data);
int GetNIPMRegBoolean(int dev, int device_address, uint8_t register_n, bool *data);
int GetNIPMRegInteger(int dev, int device_address, uint8_t register_n, int32_t *data);

void A7585_Set_V(int dev, int address, float v);
void A7585_Set_MaxV(int dev, int address, float v);
void A7585_Set_MaxI(int dev, int address, float v);
void A7585_Set_Enable(int dev, int address, bool on);
void A7585_Set_RampVs(int dev, int address, float vs);
void A7585_Set_Mode(int dev, int address, enum HVFeedbackMode fbmode);
void A7585_Set_Filter(int dev, int address, float alfa_v, float alfa_i, float alfa_t);
void A7585_Set_SiPM_Tcoef(int dev, int address, float tcomp);
void A7585_EmergencyOff(int dev, int address);
void A7585_SetI0(int dev, int address);
void A7585_Set_DigitalFB(int dev, int address, bool on);
void A7585_Set_IIC_badd(int dev, int address, uint8_t ba);

uint8_t A7585_GetDigitalPinStatus(int dev, int address, uint32_t II);
float A7585_GetVin(int dev, int address, float v);
float A7585_GetVout(int dev, int address, float v);
float A7585_GetIout(int dev, int address, float v);
float A7585_GetVref(int dev, int address, float v);
float A7585_GetTref(int dev, int address, float v);
float A7585_GetVtarget(int dev, int address, float v);
float A7585_GetVCurrentSP(int dev, int address, float v);
float A7585_GetVcorrection(int dev, int address, float v);
float A7585_Get_MaxV(int dev, int address, float v);
float A7585_Get_MaxI(int dev, int address, float v);
float A7585_Get_SiPM_Tcoef(int dev, int address, float tcomp);
bool A7585_GetVCompliance(int dev, int address, bool v);
bool A7585_GetICompliance(int dev, int address, bool v);
uint8_t A7585_Get_Mode(int dev, int address, uint32_t II);
uint8_t A7585_GetProductCode(int dev, int address, uint8_t II);
uint8_t A7585_GetFWVer(int dev, int address, uint8_t II);
uint8_t A7585_GetHWVer(int dev, int address, uint8_t II);
uint32_t A7585_GetSerialNumber(int dev, int address, uint32_t II);
bool A7585_GetHVOn(int dev, int address, bool v);
