// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 Intel Corporation.

/* System headers */
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* Public headers */
#include <led/libled.h>

/* Local headers */
#include "block.h"
#include "config.h"
#include "cntrl.h"
#include "enclosure.h"
#include "kernel_npem.h"
#include "list.h"
#include "libled_private.h"
#include "npem.h"
#include "pci_slot.h"
#include "raid.h"
#include "slot.h"
#include "sysfs.h"
#include "tail.h"
#include "utils.h"
#include "vmdssd.h"

#if _HAVE_DMALLOC_H
#include <dmalloc.h>
#endif

#define SYSFS_CLASS_BLOCK       "/sys/block"
#define SYSFS_CLASS_ENCLOSURE   "/sys/class/enclosure"
#define SYSFS_PCI_SLOTS         "/sys/bus/pci/slots"

/**
 * @brief Determine device type.
 *
 * This is internal function of sysfs module. The function determines a type of
 * RAID device either it is VOLUME or CONTAINER device. The information required
 * if read from 'metadata_version' attribute from sysfs. External and native
 * RAID devices are reported as volumes with no distinction between both types.
 *
 * @param[in]      path           Path to RAID device in sysfs tree.
 *
 * @return Type of RAID device if successful, otherwise DEVICE_TYPE_UNKNOWN.
 */
static enum device_type _get_device_type(const char *path)
{
	enum device_type result = DEVICE_TYPE_UNKNOWN;
	char *p = get_text(path, "md/metadata_version");
	if (p != NULL) {
		if (strlen(p) > 0) {
			if (strncmp(p, "external:", 9) == 0) {
				if (p[9] == '/' || p[9] == '-')
					result = DEVICE_TYPE_VOLUME;
				else
					result = DEVICE_TYPE_CONTAINER;
			} else {
				result = DEVICE_TYPE_VOLUME;
			}
		}
		free(p);
	}
	return result;
}

/**
 * @brief Gets device major and minor.
 *
 * This is internal function of sysfs module. The function retrieves major and
 * minor of device from sysfs attribute. Each block device has 'dev' attribute
 * where major and minor separated by colon are stored.
 *
 * @param[in]      path           Path to block device in sysfs tree.
 * @param[in]      d_id           Placeholder where major and minor of device
 *                                will be stored. If this argument is NULL the
 *                                behavior of function is unspecified.
 *
 * @return The function does not return a value.
 */
static void _get_id(const char *path, struct device_id *d_id)
{
	char temp[PATH_MAX];

	snprintf(temp, sizeof(temp), "%s/dev", path);
	get_id(temp, d_id);
}

/**
 * @brief Adds tail device to RAID volume.
 *
 * This is internal function of sysfs module. The function puts tail device on
 * list of tail devices of RAID volume. The memory is allocated and structure
 * fields populated. RAID device is link to tail device.
 *
 * @param[in]      path           Path to 'md' directory of RAID device in sysfs
 *                                tree.
 * @param[in]      raid           Pointer to RAID device structure corresponding
 *                                to 'path' argument.
 *
 * @return The function does not return a value.
 */
static void _tail_vol_add(struct led_ctx *ctx, const char *path, struct raid_device *raid)
{
	struct tail_device *device;

	char *t = strrchr(path, '/');
	if (strncmp(t + 1, "dev-", 4) == 0) {
		device = tail_device_init(path, &ctx->sys.sysfs_block_list);
		if (device) {
			device->raid = raid;
			list_append_ctx(&ctx->sys.tail_list, device, ctx);
		}
	}
}

/**
 * @brief Checks for duplicate entries on list of tail devices.
 *
 * This is internal function of sysfs module. The functions checks if the given
 * tail device is already on list with tail devices. This function is used by
 * _tail_cnt_add() function to avoid duplicate entries.
 *
 * @param[in]      tail          Pointer to tail device structure to check.
 *
 * @return 1 the given device is on the list, otherwise the function returns 0.
 */
static int _is_duplicate(struct list *tail_list, struct tail_device *tail)
{
	struct tail_device *device;

	list_for_each(tail_list, device) {
		if (device->block == tail->block)
			return 1;
	}
	return 0;
}

/**
 * @brief Checks if given disk can be removed from sysfs_block_list if
 * metadata is not present.
 *
 * This is internal function (action) of sysfs module. The tail_list keeps
 * all devices with metadata (raid devices). If disk is not included in tail
 * list there is not metadata on it.
 *
 * @return 1 if can be removed, otherwise 0.
 */
static int _is_non_raid_device(struct list *tail_list, struct block_device *block_device)
{
	struct tail_device *tail_device;

	list_for_each(tail_list, tail_device) {
		if (strcmp(tail_device->block->sysfs_path,
			   block_device->sysfs_path) == 0)
			return 0;
	}

	return 1;
}

/**
 */
static void _tail_cnt_add(struct led_ctx *ctx, const char *path, struct raid_device *raid)
{
	struct tail_device *device;

	char *t = strrchr(path, '/');

	if (!t)
		return;

	if (strncmp(t + 1, "dev-", 4) == 0) {
		device = tail_device_init(path, &ctx->sys.sysfs_block_list);
		if (device) {
			if (!_is_duplicate(&ctx->sys.tail_list, device)) {
				device->raid = raid;
				list_append_ctx(&ctx->sys.tail_list, device, ctx);
			} else {
				tail_device_fini(device);
			}
		}
	}
}

static void _link_raid_device(struct led_ctx *ctx, struct raid_device *device,
			      enum device_type type)
{
	char temp[PATH_MAX + strlen("/md")];
	struct list dir;

	snprintf(temp, sizeof(temp), "%s/md", device->sysfs_path);

	if (scan_dir(temp, &dir) == 0) {
		const char *dir_path;

		list_for_each(&dir, dir_path) {
			if (type == DEVICE_TYPE_VOLUME)
				_tail_vol_add(ctx, dir_path, device);
			else if (type == DEVICE_TYPE_CONTAINER)
				_tail_cnt_add(ctx, dir_path, device);
		}
		list_erase(&dir);
	}
}

/**
 */
static void _block_add(struct led_ctx *ctx, const char *path)
{
	struct block_device *device = block_device_init(&ctx->sys.cntrl_list, path);
	if (device)
		list_append_ctx(&ctx->sys.sysfs_block_list, device, ctx);
}

/**
 */
static void _volum_add(struct led_ctx *ctx, const char *path, unsigned int device_num)
{
	struct raid_device *device =
		raid_device_init(path, device_num, DEVICE_TYPE_VOLUME, ctx);
	if (device)
		list_append_ctx(&ctx->sys.volum_list, device, ctx);
}

/**
 */
static void _cntnr_add(struct led_ctx *ctx, const char *path, unsigned int device_num)
{
	struct raid_device *device =
	    raid_device_init(path, device_num, DEVICE_TYPE_CONTAINER, ctx);
	if (device)
		list_append_ctx(&ctx->sys.cntnr_list, device, ctx);
}

/**
 */
static void _raid_add(struct led_ctx *ctx, const char *path)
{
	struct device_id device_id;

	_get_id(path, &device_id);
	if (device_id.major == 9) {
		switch (_get_device_type(path)) {
		case DEVICE_TYPE_VOLUME:
			_volum_add(ctx, path, device_id.minor);
			break;
		case DEVICE_TYPE_CONTAINER:
			_cntnr_add(ctx, path, device_id.minor);
			break;
		case DEVICE_TYPE_UNKNOWN:
			break;
		}
	}
}

/**
 */
static void _cntrl_add(struct led_ctx *ctx, const char *path)
{
	struct cntrl_device *device = cntrl_device_init(path, ctx);
	if (device)
		list_append_ctx(&ctx->sys.cntrl_list, device, ctx);
}

/**
 */
static void _enclo_add(struct led_ctx *ctx, const char *path)
{
	struct enclosure_device *device = enclosure_device_init(path, ctx);

	if (device)
		list_append_ctx(&ctx->sys.enclo_list, device, ctx);
}

/**
 */
static void _pci_slots_add(struct led_ctx *ctx, const char *path)
{
	struct pci_slot *device = pci_slot_init(path, ctx);
	if (device)
		list_append_ctx(&ctx->sys.pci_slots_list, device, ctx);
}

/**
 */
static void _check_raid(struct led_ctx *ctx, const char *path)
{
	char *t = strrchr(path, '/');

	if (!t)
		return;

	if (strncmp(t + 1, "md", 2) == 0)
		_raid_add(ctx, path);
}

/**
 */
static void _check_cntrl(struct led_ctx *ctx, const char *path)
{
	char link[PATH_MAX];
	if (realpath(path, link) != NULL)
		_cntrl_add(ctx, link);
}

/**
 */
static void _check_enclo(struct led_ctx *ctx, const char *path)
{
	char link[PATH_MAX];
	if (realpath(path, link) != NULL)
		_enclo_add(ctx, link);
}

static void _scan_block(struct led_ctx *ctx)
{
	struct list dir;
	if (scan_dir(SYSFS_CLASS_BLOCK, &dir) == 0) {
		const char *dir_path;

		list_for_each(&dir, dir_path)
			_block_add(ctx, dir_path);
		list_erase(&dir);
	}
}

static void _scan_raid(struct led_ctx *ctx)
{
	struct list dir;
	if (scan_dir(SYSFS_CLASS_BLOCK, &dir) == 0) {
		const char *dir_path;

		list_for_each(&dir, dir_path)
			_check_raid(ctx, dir_path);
		list_erase(&dir);
	}
}

static void _scan_cntrl(struct led_ctx *ctx)
{
	struct list dir;
	if (scan_dir(SYSFS_PCI_DEVICES, &dir) == 0) {
		const char *dir_path;

		list_for_each(&dir, dir_path)
			_check_cntrl(ctx, dir_path);
		list_erase(&dir);
	}
}

static void _scan_tail(struct led_ctx *ctx)
{
	struct raid_device *device;

	list_for_each(&ctx->sys.volum_list, device)
		_link_raid_device(ctx, device, DEVICE_TYPE_VOLUME);
	list_for_each(&ctx->sys.cntnr_list, device)
		_link_raid_device(ctx, device, DEVICE_TYPE_CONTAINER);
	if (ctx->config.raid_members_only) {
		struct node *node;

		list_for_each_node(&ctx->sys.sysfs_block_list, node) {
			if (_is_non_raid_device(&ctx->sys.tail_list, node->item))
				list_delete(node);
		}
	}
}

static void _scan_enclo(struct led_ctx *ctx)
{
	struct list dir;
	if (scan_dir(SYSFS_CLASS_ENCLOSURE, &dir) == 0) {
		const char *dir_path;

		list_for_each(&dir, dir_path)
			_check_enclo(ctx, dir_path);
		list_erase(&dir);
	}
}

static void _scan_pci_slots(struct led_ctx *ctx)
{
	struct list dir;
	if (scan_dir(SYSFS_PCI_SLOTS, &dir) == 0) {
		const char *dir_path;

		list_for_each(&dir, dir_path) {
			if (vmdssd_check_slot_module(ctx, dir_path) == true)
				_pci_slots_add(ctx, dir_path);
		}
		list_erase(&dir);
	}
}

static void _scan_slots(struct led_ctx *ctx)
{
	struct pci_slot *pci_slot;
	struct cntrl_device *cntrl_device;
	struct enclosure_device *encl;
	struct slot_property *slot;

	list_for_each(sysfs_get_cntrl_devices(ctx), cntrl_device) {
		if (cntrl_device->cntrl_type == LED_CNTRL_TYPE_NPEM) {
			slot = npem_slot_property_init(cntrl_device);
			if (slot)
				list_append_ctx(&ctx->sys.slots_list, slot, ctx);
		} else if (cntrl_device->cntrl_type == LED_CNTRL_TYPE_KERNEL_NPEM) {
			slot = kernel_npem_slot_property_init(cntrl_device);
			if (slot)
				list_append_ctx(&ctx->sys.slots_list, slot, ctx);
		}
	}

	list_for_each(sysfs_get_pci_slots(ctx), pci_slot) {
		slot = pci_slot_property_init(pci_slot);
		if (slot)
			list_append_ctx(&ctx->sys.slots_list, slot, ctx);
	}

	list_for_each(sysfs_get_enclosure_devices(ctx), encl) {
		for (int i = 0; i < encl->slots_count; i++) {
			slot = enclosure_slot_property_init(encl, i);
			if (slot)
				list_append_ctx(&ctx->sys.slots_list, slot, ctx);
		}
	}
}

/**
 */
static int _is_failed_array(struct raid_device *raid)
{
	if (raid->degraded > 0) {
		switch (raid->level) {
		case RAID_LEVEL_1:
		case RAID_LEVEL_10:
			return (raid->degraded == raid->raid_disks);
		case RAID_LEVEL_4:
		case RAID_LEVEL_5:
			return (raid->degraded > 1);
		case RAID_LEVEL_6:
			return (raid->degraded > 2);
		case RAID_LEVEL_LINEAR:
		case RAID_LEVEL_UNKNOWN:
		case RAID_LEVEL_0:
			break;
		case RAID_LEVEL_FAULTY:
			return 1;
		}
	}
	return -1;
}

/**
 */
static void _set_block_state(struct block_device *block, enum led_ibpi_pattern ibpi)
{
	char *debug_dev = strrchr(block->sysfs_path, '/');
	debug_dev = debug_dev ? debug_dev + 1 : block->sysfs_path;

	lib_log(block->cntrl->ctx, LED_LOG_LEVEL_DEBUG,
		"(%s): device: %s, state: %s", __func__, debug_dev, ibpi2str(ibpi));
	if (block->ibpi < ibpi)
		block->ibpi = ibpi;
}

/**
 */
static void _set_array_state(struct led_ctx *ctx,
				struct raid_device *raid,
				struct block_device *block)
{
	switch (raid->sync_action) {
	case RAID_ACTION_UNKNOWN:
	case RAID_ACTION_IDLE:
	case RAID_ACTION_FROZEN:
		_set_block_state(block, LED_IBPI_PATTERN_NORMAL);
		break;
	case RAID_ACTION_RESHAPE:
		if (ctx->config.blink_on_migration)
			_set_block_state(block, LED_IBPI_PATTERN_REBUILD);
		break;
	case RAID_ACTION_CHECK:
	case RAID_ACTION_RESYNC:
	case RAID_ACTION_REPAIR:
		if (ctx->config.blink_on_init)
			_set_block_state(block, LED_IBPI_PATTERN_REBUILD);
		break;
	case RAID_ACTION_RECOVER:
		if (ctx->config.rebuild_blink_on_all)
			_set_block_state(block, LED_IBPI_PATTERN_REBUILD);
		break;
	}
}

/**
 */
static void _determine(struct led_ctx *ctx, struct tail_device *device)
{
	if (!device->block->raid_dev ||
	     (device->block->raid_dev->type == DEVICE_TYPE_CONTAINER &&
	      device->raid->type == DEVICE_TYPE_VOLUME)) {
		raid_device_fini(device->block->raid_dev);
		device->block->raid_dev = raid_device_duplicate(device->raid);
	}

	if ((device->state & TAIL_STATE_FAULTY) != 0) {
		_set_block_state(device->block, LED_IBPI_PATTERN_FAILED_DRIVE);
	} else if ((device->
	     state & (TAIL_STATE_BLOCKED | TAIL_STATE_WRITE_MOSTLY)) != 0) {
		_set_block_state(device->block, LED_IBPI_PATTERN_NORMAL);
	} else if ((device->state & TAIL_STATE_SPARE) != 0) {
		if (_is_failed_array(device->raid) == 0) {
			if (device->raid->sync_action != RAID_ACTION_RESHAPE ||
				ctx->config.blink_on_migration == 1)
				_set_block_state(device->block,
						 LED_IBPI_PATTERN_REBUILD);
		} else {
			_set_block_state(device->block, LED_IBPI_PATTERN_HOTSPARE);
		}
	} else if ((device->state & TAIL_STATE_IN_SYNC) != 0) {
		switch (_is_failed_array(device->raid)) {
		case 0:
			_set_block_state(device->block, LED_IBPI_PATTERN_DEGRADED);
			break;
		case 1:
			_set_block_state(device->block,
					 LED_IBPI_PATTERN_FAILED_ARRAY);
			break;
		}
		_set_array_state(ctx, device->raid, device->block);
	}
}

static void _determine_tails(struct led_ctx *ctx)
{
	struct tail_device *device;

	list_for_each(&ctx->sys.tail_list, device)
		_determine(ctx, device);
}

void sysfs_init(struct led_ctx *ctx)
{
	list_init(&ctx->sys.sysfs_block_list, (item_free_t)block_device_fini);
	list_init(&ctx->sys.volum_list, (item_free_t)raid_device_fini);
	list_init(&ctx->sys.cntrl_list, (item_free_t)cntrl_device_fini);
	list_init(&ctx->sys.tail_list, (item_free_t)tail_device_fini);
	list_init(&ctx->sys.cntnr_list, (item_free_t)raid_device_fini);
	list_init(&ctx->sys.enclo_list, (item_free_t)enclosure_device_fini);
	list_init(&ctx->sys.pci_slots_list, (item_free_t)pci_slot_fini);
	list_init(&ctx->sys.slots_list, NULL);
}

void sysfs_reset(struct led_ctx *ctx)
{
	list_erase(&ctx->sys.sysfs_block_list);
	list_erase(&ctx->sys.volum_list);
	list_erase(&ctx->sys.cntrl_list);
	list_erase(&ctx->sys.tail_list);
	list_erase(&ctx->sys.cntnr_list);
	list_erase(&ctx->sys.enclo_list);
	list_erase(&ctx->sys.pci_slots_list);
	list_erase(&ctx->sys.slots_list);
}

void sysfs_scan(struct led_ctx *ctx)
{
	_scan_enclo(ctx);
	_scan_cntrl(ctx);
	_scan_pci_slots(ctx);
	_scan_block(ctx);
	_scan_raid(ctx);
	_scan_slots(ctx);
	_scan_tail(ctx);

	_determine_tails(ctx);
}

/*
 * The function returns list of enclosure devices attached to SAS/SCSI storage
 * controller(s).
 */
const struct list *sysfs_get_enclosure_devices(struct led_ctx *ctx)
{
	return &ctx->sys.enclo_list;
}

/*
 * The function returns list of controller devices present in the system.
 */
const struct list *sysfs_get_cntrl_devices(struct led_ctx *ctx)
{
	return &ctx->sys.cntrl_list;
}

/*
 * The function returns list of RAID volumes present in the system.
 */
const struct list *sysfs_get_volumes(struct led_ctx *ctx)
{
	return &ctx->sys.volum_list;
}

const struct list *sysfs_get_block_devices(struct led_ctx *ctx)
{
	return &ctx->sys.sysfs_block_list;
}

const struct list *sysfs_get_pci_slots(struct led_ctx *ctx)
{
	return &ctx->sys.pci_slots_list;
}

const struct list *sysfs_get_slots(struct led_ctx *ctx)
{
	return &ctx->sys.slots_list;
}

/*
 * The function checks if the given storage controller has enclosure device(s)
 * attached.
 */
int sysfs_enclosure_attached_to_cntrl(struct led_ctx *ctx, const char *path)
{
	struct enclosure_device *device;
	size_t path_len = 0;

	if (!ctx || !path)
		return 0;

	path_len = strnlen(path, PATH_MAX);

	list_for_each(&ctx->sys.enclo_list, device) {
		if (!device)
			continue;
		if (strncmp(device->sysfs_path, path, path_len) == 0)
			return 1;
	}
	return 0;
}

/*
 * This function checks driver type.
 */
int sysfs_check_driver(const char *path, const char *driver)
{
	char buf[PATH_MAX];
	char driver_path[PATH_MAX];
	char *link;
	int found = 0;

	snprintf(buf, sizeof(buf), "%s/driver", path);
	snprintf(driver_path, sizeof(driver_path), "/%s", driver);
	link = realpath(buf, NULL);
	if (link && strstr(link, driver_path))
		found = 1;
	free(link);
	return found;
}
