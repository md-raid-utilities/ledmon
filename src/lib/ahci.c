// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 Intel Corporation.

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if _HAVE_DMALLOC_H
#include <dmalloc.h>
#endif

#include "ahci.h"
#include "config.h"
#include "utils.h"
#include "libled_private.h"

/**
 * Time interval in nano seconds to wait before enclosure management message
 * is being sent to AHCI controller.
 */
#define EM_MSG_WAIT       1500000	/* 0.0015 seconds */

/**
 * This array maps IBPI pattern to value recognized by AHCI driver. The driver
 * uses this control number to issue SGPIO signals appropriately.
 */
static const struct ibpi2value ibpi2sgpio[] = {
	{LED_IBPI_PATTERN_NORMAL, 0x00000000},
	{LED_IBPI_PATTERN_ONESHOT_NORMAL, 0x00000000},
	{LED_IBPI_PATTERN_REBUILD, 0x00480000},
	{LED_IBPI_PATTERN_FAILED_DRIVE, 0x00400000},
	{LED_IBPI_PATTERN_LOCATE, 0x00080000},
	{LED_IBPI_PATTERN_LOCATE_OFF, 0x00000000},
#ifdef DEBUG_IBPI
	{LED_IBPI_PATTERN_DEGRADED, 0x00200000},
	{LED_IBPI_PATTERN_FAILED_ARRAY, 0x00280000},
	{LED_IBPI_PATTERN_HOTSPARE, 0x01800000},
	{LED_IBPI_PATTERN_PFA, 0x01400000},
#endif
	{LED_IBPI_PATTERN_UNKNOWN, 0x00000000},
};

/*
 * The function sends a LED control message to AHCI controller. It uses
 * SGPIO to control the LEDs. See ahci.h for details.
 */
status_t ahci_sgpio_write(struct block_device *device, enum led_ibpi_pattern ibpi)
{
	char temp[WRITE_BUFFER_SIZE];
	char path[PATH_MAX + strlen("/em_message")];
	char *sysfs_path = device->cntrl_path;
	const struct timespec waittime = {
		.tv_sec = 0,
		.tv_nsec = EM_MSG_WAIT
	};
	const struct ibpi2value *ibpi2val;

	/* write only if state has changed */
	if (ibpi == device->ibpi_prev)
		return STATUS_SUCCESS;

	if (sysfs_path == NULL)
		return STATUS_NULL_POINTER;
	if ((ibpi < LED_IBPI_PATTERN_NORMAL) || (ibpi > LED_IBPI_PATTERN_LOCATE_OFF))
		return STATUS_INVALID_STATE;

	ibpi2val = get_by_ibpi(ibpi, ibpi2sgpio, ARRAY_SIZE(ibpi2sgpio));

	if (ibpi2val->ibpi == LED_IBPI_PATTERN_UNKNOWN) {
		lib_log(device->cntrl->ctx, LED_LOG_LEVEL_INFO,
			"AHCI: Controller doesn't support %s pattern\n", ibpi2str(ibpi));
		return STATUS_INVALID_STATE;
	}

	snprintf(temp, WRITE_BUFFER_SIZE, "%u", ibpi2val->value);

	snprintf(path, sizeof(path), "%s/em_message", sysfs_path);

	nanosleep(&waittime, NULL);

	if (buf_write(path, temp) != (ssize_t) strnlen(temp, WRITE_BUFFER_SIZE)) {
		lib_log(device->cntrl->ctx, LED_LOG_LEVEL_ERROR,
			"AHCI: %s write error: %d\n", path, errno);
		return STATUS_FILE_WRITE_ERROR;
	}
	return STATUS_SUCCESS;
}

#define SCSI_HOST "/scsi_host"
/*
 * The function return path to SATA port in sysfs tree. See ahci.h for details.
 */

char *ahci_get_port_path(const char *path)
{
	char *target_p, *host_p;
	size_t host_length, length_to_target;
	char *buf;

	host_p = strstr(path, "/host");
	if (host_p == NULL)
		return NULL;

	target_p = strstr(host_p, "/target");
	if (target_p == NULL)
		return NULL;

	length_to_target = target_p - path;
	host_length = target_p - host_p;

	if (host_length + length_to_target + strlen(SCSI_HOST) > PATH_MAX - 1)
		return NULL;

	buf = calloc(PATH_MAX, sizeof(char));
	if (buf == NULL)
		return NULL;

	strncpy(buf, path, length_to_target);
	strncat(buf, SCSI_HOST, strlen(SCSI_HOST) + 1);
	strncat(buf, host_p, host_length);
	return buf;
}
