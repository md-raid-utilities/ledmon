// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2023 Intel Corporation.

#include <libgen.h>
#include <string.h>
#include "config.h"

#if _HAVE_DMALLOC_H
#include <dmalloc.h>
#endif

#include "slot.h"
#include "sysfs.h"


struct slot_property *find_slot_by_device_name(struct led_ctx *ctx, char *device_name,
					       enum led_cntrl_type cntrl_type)
{
	struct slot_property *slot;

	list_for_each(sysfs_get_slots(ctx), slot) {
		if (slot->c->cntrl_type != cntrl_type)
			continue;

		if (!slot->bl_device || slot->bl_device->devnode[0] == 0)
			continue;

		if (strncmp(basename(slot->bl_device->devnode),
			    basename(device_name), PATH_MAX) == 0)
			return slot;
	}
	return NULL;
}

struct slot_property *find_slot_by_slot_path(struct led_ctx *ctx, char *slot_path,
					     enum led_cntrl_type cntrl_type)
{
	struct slot_property *slot;

	list_for_each(sysfs_get_slots(ctx), slot) {
		if (slot->c->cntrl_type != cntrl_type)
			continue;
		if (strncmp(basename(slot->slot_id), basename(slot_path), PATH_MAX) == 0)
			return slot;
	}
	return NULL;
}

status_t set_slot_pattern(struct slot_property *slot, enum led_ibpi_pattern state)
{
	return slot->c->set_slot_fn(slot, state);
}

enum led_ibpi_pattern get_slot_pattern(struct slot_property *slot)
{
	return slot->c->get_state_fn(slot);
}
