// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 Intel Corporation.
// Copyright (C) 2025 Dell Inc.

/* System headers */
#include <stdio.h>
#include <string.h>
#include <pci/pci.h>
#include <time.h>
#include <stdbool.h>
#include <sys/stat.h>

/* Local headers */
#include "libled_private.h"
#include "cntrl.h"
#include "npem.h"
#include "utils.h"

/* NPEM OK Capable/Control */
#define PCI_NPEM_OK_CAP		0x004
/* NPEM Locate Capable/Control */
#define PCI_NPEM_LOCATE_CAP	0x008
/* NPEM Fail Capable/Control */
#define PCI_NPEM_FAIL_CAP	0x010
/* NPEM Rebuild Capable/Control */
#define PCI_NPEM_REBUILD_CAP	0x020
/* NPEM Predicted Failure Analysis Capable/Control */
#define PCI_NPEM_PFA_CAP	0x040
/* NPEM Hot Spare Capable/Control */
#define PCI_NPEM_HOT_SPARE_CAP	0x080
/* NPEM in a Critical Array Capable/Control */
#define PCI_NPEM_CRA_CAP	0x100
/* NPEM in a Failed Array Capable/Control */
#define PCI_NPEM_FA_CAP		0x200

static const struct ibpi2value ibpi_to_npem_capability[] = {
	{LED_IBPI_PATTERN_NORMAL, PCI_NPEM_OK_CAP},
	{LED_IBPI_PATTERN_ONESHOT_NORMAL, PCI_NPEM_OK_CAP},
	{LED_IBPI_PATTERN_DEGRADED, PCI_NPEM_CRA_CAP},
	{LED_IBPI_PATTERN_HOTSPARE, PCI_NPEM_HOT_SPARE_CAP},
	{LED_IBPI_PATTERN_REBUILD, PCI_NPEM_REBUILD_CAP},
	{LED_IBPI_PATTERN_FAILED_ARRAY, PCI_NPEM_FA_CAP},
	{LED_IBPI_PATTERN_PFA, PCI_NPEM_PFA_CAP},
	{LED_IBPI_PATTERN_FAILED_DRIVE, PCI_NPEM_FAIL_CAP},
	{LED_IBPI_PATTERN_LOCATE, PCI_NPEM_LOCATE_CAP},
	{LED_IBPI_PATTERN_LOCATE_OFF, PCI_NPEM_OK_CAP},
	{LED_IBPI_PATTERN_UNKNOWN, 0}
};

struct kernel_npem_led {
	int	bitmask;
	char	*sysfs_led_name;
};

const struct kernel_npem_led kernel_npem_leds[] = {
	{PCI_NPEM_OK_CAP, "enclosure:ok"},
	{PCI_NPEM_LOCATE_CAP, "enclosure:locate"},
	{PCI_NPEM_FAIL_CAP, "enclosure:fail"},
	{PCI_NPEM_REBUILD_CAP, "enclosure:rebuild"},
	{PCI_NPEM_PFA_CAP, "enclosure:pfa"},
	{PCI_NPEM_HOT_SPARE_CAP, "enclosure:hotspare"},
	{PCI_NPEM_CRA_CAP, "enclosure:ica"},
	{PCI_NPEM_FA_CAP, "enclosure:ifa"},
};

char *kernel_npem_get_path(const char *cntrl_path)
{
	return strdup(cntrl_path);
}

#define make_led_path(sysfs_led_path, sysfs_path, sysfs_led_name) \
	snprintf(sysfs_led_path, sizeof(sysfs_led_path), "%s/leds/%s:%s/brightness", \
		 sysfs_path, basename(sysfs_path), sysfs_led_name)

static u32 read_kernel_npem_register(const char *sysfs_path)
{
	char led_path[PATH_MAX];
	int i;
	u32 reg = 0;

	for (i = 0; i < ARRAY_SIZE(kernel_npem_leds); i++) {
		make_led_path(led_path, sysfs_path, kernel_npem_leds[i].sysfs_led_name);
		reg |= get_int("/", 0, led_path) ? kernel_npem_leds[i].bitmask : 0;
	}
	return reg;
}

static int write_kernel_npem_register(const char *sysfs_path, u32 val)
{
	char led_path[PATH_MAX], val_text[4];
	int i;
	struct stat sb;

	for (i = 0; i < ARRAY_SIZE(kernel_npem_leds); i++) {
		make_led_path(led_path, sysfs_path, kernel_npem_leds[i].sysfs_led_name);
		snprintf(val_text, sizeof(val_text), val & kernel_npem_leds[i].bitmask ? "1" : "0");
		if (!stat(led_path, &sb))
			buf_write(led_path, val_text);
	}
	return 0;
}

static u32 kernel_npem_supported_mask(const char *sysfs_path)
{
	char led_path[PATH_MAX];
	int i;
	struct stat sb;
	u32 supported = 0;

	for (i = 0; i < ARRAY_SIZE(kernel_npem_leds); i++) {
		make_led_path(led_path, sysfs_path, kernel_npem_leds[i].sysfs_led_name);
		if (!stat(led_path, &sb))
			supported |= kernel_npem_leds[i].bitmask;
	}
	return supported;
}

int is_kernel_npem_present(const char *path)
{
	return kernel_npem_supported_mask(path) ? 1 : 0;
}

enum led_ibpi_pattern kernel_npem_get_state(struct slot_property *slot)
{
	const char *path = slot->slot_spec.cntrl->sysfs_path;
	const struct ibpi2value *ibpi2val;
	u32 reg;

	reg = read_kernel_npem_register(path);
	ibpi2val =  get_by_bits(reg, ibpi_to_npem_capability,
				ARRAY_SIZE(ibpi_to_npem_capability));

	return ibpi2val->ibpi;
}

status_t kernel_npem_set_slot(struct led_ctx *ctx, const char *sysfs_path,
			      enum led_ibpi_pattern state)
{
	const struct ibpi2value *ibpi2val;
	u32 requested, supported;

	ibpi2val = get_by_ibpi(state, ibpi_to_npem_capability,
			       ARRAY_SIZE(ibpi_to_npem_capability));

	if (ibpi2val->ibpi == LED_IBPI_PATTERN_UNKNOWN) {
		lib_log(ctx, LED_LOG_LEVEL_INFO,
			"KERNEL_NPEM: Controller doesn't support %s pattern\n", ibpi2str(state));
		return STATUS_INVALID_STATE;
	}

	requested = (u32)ibpi2val->value;
	supported = kernel_npem_supported_mask(sysfs_path);

	if (!(requested & supported))
		/*
		 * Allow OK (normal and locate_off states) to turn off other
		 * states even if OK state isn't actually supported.
		 */
		if (requested != PCI_NPEM_OK_CAP) {
			lib_log(ctx, LED_LOG_LEVEL_INFO,
				"KERNEL_NPEM: Controller %s doesn't support %s pattern\n",
				sysfs_path, ibpi2str(state));
			return STATUS_INVALID_STATE;
		}

	write_kernel_npem_register(sysfs_path, requested);

	return STATUS_SUCCESS;
}

status_t kernel_npem_set_state(struct slot_property *slot, enum led_ibpi_pattern state)
{
	return kernel_npem_set_slot(slot->slot_spec.cntrl->ctx,
				    slot->slot_spec.cntrl->sysfs_path, state);
}

const struct slot_property_common kernel_npem_slot_common = {
	.cntrl_type = LED_CNTRL_TYPE_KERNEL_NPEM,
	.get_state_fn = kernel_npem_get_state,
	.set_slot_fn = kernel_npem_set_state,
};

struct slot_property *kernel_npem_slot_property_init(struct cntrl_device *kernel_npem_cntrl)
{
	struct slot_property *result = calloc(1, sizeof(struct slot_property));

	if (result == NULL)
		return NULL;

	result->bl_device = get_block_device_from_sysfs_path(kernel_npem_cntrl->ctx,
							     kernel_npem_cntrl->sysfs_path, true);
	result->slot_spec.cntrl = kernel_npem_cntrl;
	snprintf(result->slot_id, PATH_MAX, "%s", kernel_npem_cntrl->sysfs_path);
	result->c = &kernel_npem_slot_common;
	return result;
}

status_t kernel_npem_write(struct block_device *device, enum led_ibpi_pattern ibpi)
{
	if (ibpi < LED_IBPI_PATTERN_NORMAL || ibpi > LED_IBPI_PATTERN_LOCATE_OFF)
		return STATUS_INVALID_STATE;

	return kernel_npem_set_slot(device->cntrl->ctx, device->cntrl->sysfs_path, ibpi);
}
