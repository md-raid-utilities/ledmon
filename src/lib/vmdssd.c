// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 Intel Corporation.

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "list.h"
#include "pci_slot.h"
#include "status.h"
#include "sysfs.h"
#include "utils.h"
#include "vmdssd.h"
#include "libled_private.h"

#define ATTENTION_OFF        0xF  /* (1111) Attention Off, Power Off */
#define ATTENTION_LOCATE     0x7  /* (0111) Attention Off, Power On */
#define ATTENTION_REBUILD    0x5  /* (0101) Attention On, Power On */
#define ATTENTION_FAILURE    0xD  /* (1101) Attention On, Power Off */

struct ibpi2value ibpi_to_attention[] = {
	{LED_IBPI_PATTERN_NORMAL, ATTENTION_OFF},
	{LED_IBPI_PATTERN_LOCATE, ATTENTION_LOCATE},
	{LED_IBPI_PATTERN_FAILED_DRIVE, ATTENTION_FAILURE},
	{LED_IBPI_PATTERN_REBUILD, ATTENTION_REBUILD},
	{LED_IBPI_PATTERN_LOCATE_OFF, ATTENTION_OFF},
	{LED_IBPI_PATTERN_ONESHOT_NORMAL, ATTENTION_OFF},
	{LED_IBPI_PATTERN_UNKNOWN, 0}
};

#define SYSFS_PCIEHP         "/sys/module/pciehp"
#define SYSFS_VMD            "/sys/bus/pci/drivers/vmd"

static char *get_slot_from_syspath(const char *path)
{
	char *cur, *ret = NULL;
	char *temp_path = strdup(path);

	if (!temp_path)
		return NULL;

	cur = strtok(temp_path, "/");
	while (cur != NULL) {
		char *next = strtok(NULL, "/");

		if ((next != NULL) && strcmp(next, "nvme") == 0)
			break;
		cur = next;
	}

	cur = strtok(cur, ".");
	if (cur)
		ret = strdup(cur);
	free(temp_path);

	return ret;
}

char *vmdssd_get_domain(const char *path)
{
	char domain_path[PATH_MAX], real_domain_path[PATH_MAX];

	snprintf(domain_path, PATH_MAX, "%s/%s/domain",
		 SYSFS_VMD, basename(path));
	if (realpath(domain_path, real_domain_path) == NULL)
		return NULL;

	return strtok(basename(real_domain_path), ":");
}

bool vmdssd_check_slot_module(struct led_ctx *ctx, const char *slot_path)
{
	char *address;
	struct cntrl_device *cntrl;

	address = get_text(slot_path, "address");
	if (address == NULL)
		return false;

	// check if slot address contains vmd domain
	list_for_each(sysfs_get_cntrl_devices(ctx), cntrl) {
		if (cntrl->cntrl_type == LED_CNTRL_TYPE_VMD) {
			if (cntrl->domain[0] == '\0')
				continue;
			if (strstr(address, cntrl->domain) == NULL)
				continue;
			free(address);
			return true;
		}
	}

	free(address);
	return false;
}

struct pci_slot *vmdssd_find_pci_slot(struct led_ctx *ctx, const char *device_path)
{
	char *pci_addr;
	struct pci_slot *slot = NULL;

	pci_addr = get_slot_from_syspath(device_path);
	if (!pci_addr)
		return NULL;

	list_for_each(sysfs_get_pci_slots(ctx), slot) {
		if (strcmp(slot->address, pci_addr) == 0)
			break;
		slot = NULL;
	}
	free(pci_addr);
	if (slot == NULL || !vmdssd_check_slot_module(ctx, slot->sysfs_path))
		return NULL;

	return slot;
}

enum led_ibpi_pattern vmdssd_get_attention(struct pci_slot *slot)
{
	int attention = get_int(slot->sysfs_path, -1, "attention");
	const struct ibpi2value *ibpi2val;

	if (attention == -1)
		return LED_IBPI_PATTERN_UNKNOWN;

	ibpi2val = get_by_value(attention, ibpi_to_attention, ARRAY_SIZE(ibpi_to_attention));
	return ibpi2val->ibpi;
}

status_t vmdssd_write_attention_buf(struct pci_slot *slot, enum led_ibpi_pattern ibpi)
{
	char attention_path[PATH_MAX + strlen("/attention")];
	char buf[WRITE_BUFFER_SIZE];
	const struct ibpi2value *ibpi2val;

	uint16_t val;

	lib_log(slot->ctx, LED_LOG_LEVEL_DEBUG,
		"%s before: 0x%x\n", slot->address,
		(unsigned int)get_int(slot->sysfs_path, 0, "attention"));

	ibpi2val = get_by_ibpi(ibpi, ibpi_to_attention, ARRAY_SIZE(ibpi_to_attention));
	if (ibpi2val->ibpi == LED_IBPI_PATTERN_UNKNOWN) {

		lib_log(slot->ctx, LED_LOG_LEVEL_INFO,
			"VMD: Controller doesn't support %s pattern\n", ibpi2str(ibpi));
		return STATUS_INVALID_STATE;
	}
	val = (uint16_t)ibpi2val->value;

	snprintf(buf, WRITE_BUFFER_SIZE, "%u", val);
	snprintf(attention_path, sizeof(attention_path), "%s/attention", slot->sysfs_path);
	if (buf_write(attention_path, buf) != (ssize_t) strnlen(buf, WRITE_BUFFER_SIZE)) {
		lib_log(slot->ctx, LED_LOG_LEVEL_ERROR,
			"%s write error: %d\n", slot->sysfs_path, errno);
		return STATUS_FILE_WRITE_ERROR;
	}
	lib_log(slot->ctx, LED_LOG_LEVEL_DEBUG,
		"%s after: 0x%x\n", slot->address,
		(unsigned int)get_int(slot->sysfs_path, 0, "attention"));

	return STATUS_SUCCESS;
}

status_t vmdssd_write(struct block_device *device, enum led_ibpi_pattern ibpi)
{
	struct pci_slot *slot;
	char *short_name = strrchr(device->sysfs_path, '/');

	if (short_name)
		short_name++;
	else
		short_name = device->sysfs_path;

	if (ibpi == device->ibpi_prev)
		return STATUS_SUCCESS;

	if ((ibpi < LED_IBPI_PATTERN_NORMAL) || (ibpi > LED_IBPI_PATTERN_LOCATE_OFF))
		return STATUS_INVALID_STATE;

	slot = vmdssd_find_pci_slot(device->cntrl->ctx, device->sysfs_path);
	if (!slot) {
		lib_log(device->cntrl->ctx, LED_LOG_LEVEL_DEBUG,
			"PCI hotplug slot not found for %s\n", short_name);
		return STATUS_NULL_POINTER;
	}

	return vmdssd_write_attention_buf(slot, ibpi);
}

char *vmdssd_get_path(const char *cntrl_path)
{
	return strdup(cntrl_path);
}
