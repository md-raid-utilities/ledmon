// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 Intel Corporation.

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config.h"
#include "led/libled.h"


#if _HAVE_DMALLOC_H
#include <dmalloc.h>
#endif


#include "pci_slot.h"

#include "utils.h"
#include "vmdssd.h"

/*
 * Allocates memory for PCI hotplug slot structure and initializes fields of
 * the structure.
 */
struct pci_slot *pci_slot_init(const char *path, struct led_ctx *ctx)
{
	struct pci_slot *result = NULL;

	result = calloc(1, sizeof(struct pci_slot));
	if (result == NULL)
		return NULL;
	strncpy(result->sysfs_path, path, PATH_MAX - 1);
	result->address = get_text(path, "address");
	if (!result->address)
		goto error;

	result->ctx = ctx;

	return result;
error:
	free(result->address);
	free(result);
	return NULL;
}

/*
 * The function returns memory allocated for fields of hotplug slot structure
 * to the system.
 */
void pci_slot_fini(struct pci_slot *slot)
{
	if (slot) {
		free(slot->address);
		free(slot);
	}
}

const struct slot_property_common pci_slot_common = {
	.cntrl_type = LED_CNTRL_TYPE_VMD,
	.get_state_fn = pci_get_state,
	.set_slot_fn = pci_set_slot
};

struct slot_property *pci_slot_property_init(struct pci_slot *pci_slot)
{
	struct slot_property *result = calloc(1, sizeof(struct slot_property));
	if (result == NULL)
		return NULL;

	result->bl_device = get_block_device_from_sysfs_path(pci_slot->ctx,
							     pci_slot->address, true);
	if (result->bl_device) {
		struct stat st;

		if (stat(result->bl_device->devnode, &st) != 0)
			result->bl_device = NULL;
	}
	result->slot_spec.pci = pci_slot;
	snprintf(result->slot_id, PATH_MAX, "%s", pci_slot->sysfs_path);
	result->c = &pci_slot_common;

	return result;
}

status_t pci_set_slot(struct slot_property *slot, enum led_ibpi_pattern state)
{
	return vmdssd_write_attention_buf(slot->slot_spec.pci, state);
}

enum led_ibpi_pattern pci_get_state(struct slot_property *slot)
{
	return vmdssd_get_attention(slot->slot_spec.pci);
}
