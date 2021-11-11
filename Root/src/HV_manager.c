#include "HV_manager.h"

#define verbose false

int SetNIPMRegBoolean(int dev, int device_address, uint8_t register_n, bool data)
{
	uint8_t vv[16];
	vv[0] = register_n;
	vv[1] = 2;
	vv[2] = data == true ? 1 : 0;
	vv[3] = 0;
	vv[4] = 0;
	vv[5] = 0;
	if (ioctl(dev, I2C_SLAVE, device_address) < 0)
	{
		return -1;
	}
	else
	{
		return write(dev, vv, 6);
	}
}

void SetNIPMRegFloat(int dev, int device_address, uint8_t register_n, float data)
{
	ssize_t ret;
	uint8_t vv[16];
	vv[0] = register_n;
	vv[1] = 3;
	memcpy(&vv[2], &data, 4);
	if (ioctl(dev, I2C_SLAVE, device_address) < 0)
	{
		if (verbose)
		{
			printf("iic_disp_error_on_write!\n");
		}
	}
	else
	{
		ret = write(dev, vv, 6);
	}
}

void SetNIPMRegInteger(int dev, int device_address, uint8_t register_n, int32_t data)
{
	ssize_t ret;
	uint8_t vv[16];
	vv[0] = register_n;
	vv[1] = 0;
	memcpy(&vv[2], &data, 4);
	if (ioctl(dev, I2C_SLAVE, device_address) < 0)
	{
		if (verbose)
		{
			printf("iic_disp_error_on_write!\n");
		}
	}
	else
	{
		ret = write(dev, vv, 6);
	}
}

int GetNIPMRegFloat(int dev, int device_address, uint8_t register_n, float *data)
{
	ssize_t ret;

	uint8_t vv[16];
	vv[0] = register_n;
	vv[1] = 3;

	if (ioctl(dev, I2C_SLAVE, device_address) < 0)
	{
		if (verbose)
		{
			printf("iic_disp_error_on_write!\n");
		}
		return -1;
	}
	else
	{
		ret = write(dev, vv, 2);
		ret = read(dev, vv, 4);
	}

	if (verbose)
	{
		printf("%X %X %X %X\n", vv[0], vv[1], vv[2], vv[3]);
	}
	memcpy(data, &vv, 4);
	return 1;
}

int GetNIPMRegBoolean(int dev, int device_address, uint8_t register_n, bool *data)
{
	ssize_t ret;

	uint8_t vv[16];
	vv[0] = register_n;
	vv[1] = 2;

	if (ioctl(dev, I2C_SLAVE, device_address) < 0)
	{
		if (verbose)
		{
			printf("iic_disp_error_on_read!\n");
		}
		return -1;
	}
	else
	{

		ret = write(dev, vv, 2);
		ret = read(dev, vv, 4);
	}
	*data = vv[0];
	return 1;
}

int GetNIPMRegInteger(int dev, int device_address, uint8_t register_n, uint32_t *data)
{
	ssize_t ret;

	uint8_t vv[16];
	vv[0] = register_n;
	vv[1] = 0;

	if (ioctl(dev, I2C_SLAVE, device_address) < 0)
	{
		if (verbose)
		{
			printf("iic_disp_error_on_read!\n");
		}
		return -1;
	}
	else
	{
		ret = write(dev, vv, 2);
		ret = read(dev, vv, 4);
	}
	memcpy(data, vv, 4);
	return 1;
}

void A7585_Set_V(int dev, int address, float v)
{
	SetNIPMRegFloat(dev, address, NIPMREG_VTARGET, v);
}

void A7585_Set_MaxV(int dev, int address, float v)
{
	SetNIPMRegFloat(dev, address, NIPMREG_MAXV, v);
}

void A7585_Set_MaxI(int dev, int address, float v)
{
	SetNIPMRegFloat(dev, address, NIPMREG_MAXI, v);
}

void A7585_Set_Enable(int dev, int address, bool on)
{
	SetNIPMRegBoolean(dev, address, NIPMREG_HVSTATUS, on);
}

void A7585_Set_RampVs(int dev, int address, float vs)
{
	SetNIPMRegFloat(dev, address, NIPMREG_RAMP, vs);
}

void A7585_Set_Mode(int dev, int address, enum HVFeedbackMode fbmode)
{
	SetNIPMRegInteger(dev, address, NIPMREG_FBMODE, fbmode);
}

void A7585_Set_Filter(int dev, int address, float alfa_v, float alfa_i, float alfa_t)
{
	SetNIPMRegFloat(dev, address, NIPMREG_ALFA_V, alfa_v);
	SetNIPMRegFloat(dev, address, NIPMREG_ALFA_I, alfa_i);
	SetNIPMRegFloat(dev, address, NIPMREG_ALFA_TREF, alfa_t);
}

void A7585_Set_SiPM_Tcoef(int dev, int address, float tcomp)
{
	SetNIPMRegFloat(dev, address, NIPMREG_TCOEF, tcomp);
}

void A7585_EmergencyOff(int dev, int address)
{
	SetNIPMRegBoolean(dev, address, NIPMREG_EMERGENCYSTOP, 1);
}

void A7585_SetI0(int dev, int address)
{
	SetNIPMRegBoolean(dev, address, NIPMREG_IZERO, 1);
}

void A7585_Set_DigitalFB(int dev, int address, bool on)
{
	SetNIPMRegBoolean(dev, address, NIPMREG_PIDENABLE, on);
}

void A7585_Set_IIC_badd(int dev, int address, uint8_t ba)
{
	SetNIPMRegInteger(dev, address, NIPMREG_IIC_BA, (int)ba);
}

uint8_t A7585_GetDigitalPinStatus(int dev, int address, uint32_t II)
{
	GetNIPMRegInteger(dev, address, NIPMREG_DIGITALIO, &II);
	return II;
}

float A7585_Get_MaxV(int dev, int address, float v)
{
	GetNIPMRegFloat(dev, address, NIPMREG_MAXV, &v);
	return v;
}

float A7585_Get_MaxI(int dev, int address, float v)
{
	GetNIPMRegFloat(dev, address, NIPMREG_MAXI, &v);
	return v;
}

float A7585_Get_SiPM_Tcoef(int dev, int address, float tcomp)
{
	GetNIPMRegFloat(dev, address, NIPMREG_TCOEF, &tcomp);
	return tcomp;
}

float A7585_GetVin(int dev, int address, float v)
{
	GetNIPMRegFloat(dev, address, NIPMREG_VIN, &v);
	return v;
}

float A7585_GetVout(int dev, int address, float v)
{
	GetNIPMRegFloat(dev, address, NIPMREG_VOUT, &v);
	return v;
}

float A7585_GetIout(int dev, int address, float v)
{
	GetNIPMRegFloat(dev, address, NIPMREG_IOUT, &v);
	return v;
}

float A7585_GetVref(int dev, int address, float v)
{
	GetNIPMRegFloat(dev, address, NIPMREG_VREF, &v);
	return v;
}

float A7585_GetTref(int dev, int address, float v)
{
	GetNIPMRegFloat(dev, address, NIPMREG_TREF, &v);
	return v;
}

float A7585_GetVtarget(int dev, int address, float v)
{
	GetNIPMRegFloat(dev, address, NIPMREG_VTARGET_R, &v);
	return v;
}

float A7585_GetVCurrentSP(int dev, int address, float v)
{
	GetNIPMRegFloat(dev, address, NIPMREG_RTARGET_R, &v);
	return v;
}

float A7585_GetVcorrection(int dev, int address, float v)
{
	GetNIPMRegFloat(dev, address, NIPMREG_CORRECTIONVOLTAGE, &v);
	return v;
}

uint8_t A7585_Get_Mode(int dev, int address, uint32_t II)
{
	GetNIPMRegInteger(dev, address, NIPMREG_FBMODE, &II);
	return II;
}

bool A7585_GetVCompliance(int dev, int address, bool v)
{
	GetNIPMRegBoolean(dev, address, NIPMREG_COMPV, &v);
	return v;
}

bool A7585_GetICompliance(int dev, int address, bool v)
{
	GetNIPMRegBoolean(dev, address, NIPMREG_COMPI, &v);
	return v;
}

uint8_t A7585_GetProductCode(int dev, int address, uint32_t II)
{
	GetNIPMRegInteger(dev, address, NIPMREG_PDCODE, &II);
	return II;
}

uint8_t A7585_GetFWVer(int dev, int address, uint32_t II)
{
	GetNIPMRegInteger(dev, address, NIPMREG_FWVER, &II);
	return II;
}

uint8_t A7585_GetHWVer(int dev, int address, uint32_t II)
{
	GetNIPMRegInteger(dev, address, NIPMREG_HWVER, &II);
	return II;
}

uint32_t A7585_GetSerialNumber(int dev, int address, uint32_t II)
{
	GetNIPMRegInteger(dev, address, NIPMREG_SN, &II);
	return II;
}

bool A7585_GetHVOn(int dev, int address, bool v)
{
	GetNIPMRegBoolean(dev, address, NIPMREG_HVSTATUS, &v);
	return v;
}
