// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2023, Dell Inc.

/* Dell Backplane LED control */

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <linux/ipmi.h>
#include "config.h"

#if _HAVE_DMALLOC_H
#include <dmalloc.h>
#endif

#include "libled_private.h"
#include "cntrl.h"

#include "dellssd.h"
#include "led/libled.h"
#include "smp.h"
#include "status.h"
#include "utils.h"
#include "ipmi.h"

#define BP_PRESENT       (1L << 0)
#define BP_ONLINE        (1L << 1)
#define BP_HOTSPARE      (1L << 2)
#define BP_IDENTIFY      (1L << 3)
#define BP_REBUILDING    (1L << 4)
#define BP_FAULT         (1L << 5)
#define BP_PREDICT       (1L << 6)
#define BP_CRITICALARRAY (1L << 9)
#define BP_FAILEDARRAY   (1L << 10)

static const struct ibpi2value ibpi2ssd[] = {
	{LED_IBPI_PATTERN_NORMAL, BP_ONLINE},
	{LED_IBPI_PATTERN_ONESHOT_NORMAL, BP_ONLINE},
	{LED_IBPI_PATTERN_DEGRADED, BP_CRITICALARRAY | BP_ONLINE},
	{LED_IBPI_PATTERN_HOTSPARE, BP_HOTSPARE | BP_ONLINE},
	{LED_IBPI_PATTERN_REBUILD, BP_REBUILDING | BP_ONLINE},
	{LED_IBPI_PATTERN_FAILED_ARRAY, BP_FAILEDARRAY | BP_ONLINE},
	{LED_IBPI_PATTERN_PFA, BP_PREDICT | BP_ONLINE},
	{LED_IBPI_PATTERN_FAILED_DRIVE, BP_FAULT | BP_ONLINE},
	{LED_IBPI_PATTERN_LOCATE, BP_IDENTIFY | BP_ONLINE},
	{LED_IBPI_PATTERN_LOCATE_OFF, BP_ONLINE},
	{LED_IBPI_PATTERN_UNKNOWN, 0}
};

#define DELL_OEM_NETFN                      0x30

#define DELL_OEM_STORAGE_CMD                0xD5
#define DELL_OEM_STORAGE_GETDRVMAP_12G      0x07
#define DELL_OEM_STORAGE_SETDRVSTATUS_12G   0x04
#define DELL_OEM_STORAGE_GETDRVMAP_13G      0x17
#define DELL_OEM_STORAGE_SETDRVSTATUS_13G   0x14
#define DELL_OEM_STORAGE_GETDRVMAP_14G      0x37
#define DELL_OEM_STORAGE_SETDRVSTATUS_14G   0x34

#define APP_NETFN			    0x06
#define APP_GET_SYSTEM_INFO		    0x59
#define DELL_GET_IDRAC_INFO		    0xDD

/*
 * The dellssd (IPMI) method of controlling LEDs is deprecated in
 * current Dell systems, in favor of the _DSM which Dell systems
 * support since 17G. The _DSM does not use this code, so future
 * updates to add later Dell systems here should not be needed.
 */
enum {
  DELL_12G_MONOLITHIC = 0x10,
  DELL_12G_MODULAR    = 0x11,
  DELL_13G_MONOLITHIC = 0x20,
  DELL_13G_MODULAR    = 0x21,
  DELL_14G_MONOLITHIC = 0x30,
  DELL_14G_MODULAR    = 0x31,
  DELL_15G_MONOLITHIC = 0x40,
  DELL_15G_MODULAR    = 0x41,
  DELL_16G_MONOLITHIC = 0x50,
  DELL_16G_MODULAR    = 0x51,
  DELL_17G_MONOLITHIC = 0x60,
  DELL_17G_MODULAR    = 0x61,
  DELL_18G_MONOLITHIC = 0x70,
  DELL_18G_MODULAR    = 0x71,

};

int get_dell_server_type(struct led_ctx *ctx)
{
	uint8_t data[4], rdata[20];
	int rc, rlen;

	/* Don't requery if we already know have ID */
	if (ctx->dellssd_hw_gen)
		return ctx->dellssd_hw_gen;

	/* Get Dell Generation */
	memset(data, 0, sizeof(data));
	memset(rdata, 0, sizeof(rdata));
	data[0] = 0x00;
	data[1] = DELL_GET_IDRAC_INFO;
	data[2] = 0x02;
	data[3] = 0x00;
	rc = ipmicmd(ctx, BMC_TA, 0, APP_NETFN, APP_GET_SYSTEM_INFO, 4, data,
		     20, &rlen, rdata);
	if (rc) {
		lib_log(ctx, LED_LOG_LEVEL_DEBUG, "Unable to issue IPMI command GetSystemInfo\n");
		return 0;
	}
	switch (rdata[10]) {
	case DELL_12G_MONOLITHIC:
	case DELL_12G_MODULAR:
	case DELL_13G_MONOLITHIC:
	case DELL_13G_MODULAR:
	case DELL_14G_MONOLITHIC:
	case DELL_14G_MODULAR:
	case DELL_15G_MONOLITHIC:
	case DELL_15G_MODULAR:
	case DELL_16G_MONOLITHIC:
	case DELL_16G_MODULAR:
	case DELL_17G_MONOLITHIC:
	case DELL_17G_MODULAR:
	case DELL_18G_MONOLITHIC:
	case DELL_18G_MODULAR:

		ctx->dellssd_hw_gen = rdata[10];
		return ctx->dellssd_hw_gen;
	default:
		lib_log(ctx, LED_LOG_LEVEL_DEBUG, "Unable to determine Dell Server type\n");
		break;
	}
	return 0;
}

static status_t ipmi_setled(struct led_ctx *ctx, int b, int d, int f, int state)
{
	uint8_t data[20], rdata[20];
	int rc, rlen, bay = 0xFF, slot = 0xFF, devfn, gen = 0;

	/* Check if this is a supported Dell server */
	gen = get_dell_server_type(ctx);
	if (!gen)
		return STATUS_FILE_WRITE_ERROR;
	devfn = (((d & 0x1F) << 3) | (f & 0x7));

	/* Get mapping of BDF to bay:slot */
	memset(data, 0, sizeof(data));
	memset(rdata, 0, sizeof(rdata));
	data[0] = 0x01;				/* get         */
	data[2] = 0x06;				/* length lsb  */
	data[3] = 0x00;				/* length msb  */
	data[4] = 0x00;				/* offset lsb  */
	data[5] = 0x00;				/* offset msb  */
	data[6] = b;				/* bus         */
	data[7] = devfn;			/* devfn       */
	switch (gen) {
	case DELL_12G_MONOLITHIC:
	case DELL_12G_MODULAR:
		data[1] = DELL_OEM_STORAGE_GETDRVMAP_12G;
		break;
	case DELL_13G_MONOLITHIC:
	case DELL_13G_MODULAR:
		data[1] = DELL_OEM_STORAGE_GETDRVMAP_13G;
		break;
	case DELL_14G_MONOLITHIC:
	case DELL_14G_MODULAR:
	case DELL_15G_MONOLITHIC:
	case DELL_15G_MODULAR:
	case DELL_16G_MONOLITHIC:
	case DELL_16G_MODULAR:
	case DELL_17G_MONOLITHIC:
	case DELL_17G_MODULAR:
	case DELL_18G_MONOLITHIC:
	case DELL_18G_MODULAR:

		data[1] = DELL_OEM_STORAGE_GETDRVMAP_14G;
		break;
	}
	rc = ipmicmd(ctx, BMC_TA, 0, DELL_OEM_NETFN, DELL_OEM_STORAGE_CMD, 8, data,
		     20, &rlen, rdata);
	if (!rc) {
		bay = rdata[7];
		slot = rdata[8];
	}
	if (bay == 0xFF || slot == 0xFF) {
		lib_log(ctx, LED_LOG_LEVEL_ERROR,
			"Unable to determine bay/slot for device %.2x:%.2x.%x\n",
			(unsigned int)b, (unsigned int)d, (unsigned int)f);
		return STATUS_FILE_WRITE_ERROR;
	}

	/* Set Bay:Slot to Mask */
	memset(data, 0, sizeof(data));
	memset(rdata, 0, sizeof(rdata));
	data[0] = 0x00;					/* set              */
	data[2] = 0x0e;					/* length lsb       */
	data[3] = 0x00;					/* length msb       */
	data[4] = 0x00;					/* offset lsb       */
	data[5] = 0x00;					/* offset msb       */
	data[6] = 0x0e;					/* length lsb       */
	data[7] = 0x00;					/* length msb       */
	data[8] = bay;					/* bayid            */
	data[9] = slot;					/* slotid           */
	data[10] = state & 0xff;			/* state LSB        */
	data[11] = state >> 8;				/* state MSB        */
	switch (gen) {
	case DELL_12G_MONOLITHIC:
	case DELL_12G_MODULAR:
		data[1] = DELL_OEM_STORAGE_SETDRVSTATUS_12G;
		break;
	case DELL_13G_MONOLITHIC:
	case DELL_13G_MODULAR:
		data[1] = DELL_OEM_STORAGE_SETDRVSTATUS_13G;
		break;
	case DELL_14G_MONOLITHIC:
	case DELL_14G_MODULAR:
	case DELL_15G_MONOLITHIC:
	case DELL_15G_MODULAR:
	case DELL_16G_MONOLITHIC:
	case DELL_16G_MODULAR:
	case DELL_17G_MONOLITHIC:
	case DELL_17G_MODULAR:
	case DELL_18G_MONOLITHIC:
	case DELL_18G_MODULAR:

		data[1] = DELL_OEM_STORAGE_SETDRVSTATUS_14G;
		break;
	}
	rc = ipmicmd(ctx, BMC_TA, 0, DELL_OEM_NETFN, DELL_OEM_STORAGE_CMD, 20, data,
		     20, &rlen, rdata);
	if (rc) {
		lib_log(ctx, LED_LOG_LEVEL_ERROR,
			"Unable to issue SetDriveState for %.2x:%.2x.%x\n",
			(unsigned int)b, (unsigned int)d, (unsigned int)f);
		return STATUS_FILE_WRITE_ERROR;
	}
	return STATUS_SUCCESS;
}

char *dellssd_get_path(const char *cntrl_path)
{
	return strdup(cntrl_path);
}

status_t dellssd_write(struct block_device *device, enum led_ibpi_pattern ibpi)
{
	unsigned int bus, dev, fun;
	char *t;
	const struct ibpi2value *ibpi2val;

	/* write only if state has changed */
	if (ibpi == device->ibpi_prev)
		return STATUS_SUCCESS;

	if ((ibpi < LED_IBPI_PATTERN_NORMAL) || (ibpi > LED_IBPI_PATTERN_LOCATE_OFF))
		return STATUS_INVALID_STATE;

	ibpi2val = get_by_ibpi(ibpi, ibpi2ssd, ARRAY_SIZE(ibpi2ssd));

	if (ibpi2val->ibpi == LED_IBPI_PATTERN_UNKNOWN) {

		lib_log(device->cntrl->ctx, LED_LOG_LEVEL_INFO,
			"SSD: Controller doesn't support %s pattern\n", ibpi2str(ibpi));
		return STATUS_INVALID_STATE;
	}

	t = strrchr(device->cntrl_path, '/');

	/* Extract PCI bus:device.function */
	if (t == NULL || sscanf(t + 1, "%*x:%x:%x.%x", &bus, &dev, &fun) != 3)
		return STATUS_DATA_ERROR;

	return ipmi_setled(device->cntrl->ctx, bus, dev, fun, ibpi2val->value);
}
