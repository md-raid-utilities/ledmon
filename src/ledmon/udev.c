// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 Intel Corporation.

/* System headers */
#include <errno.h>
#include <limits.h>
#include <libudev.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Public headers */
#include <led/libled.h>
#include <lib/utils.h>

/* Local headers */
#include "block.h"
#include "status.h"
#include "sysfs.h"
#include "udev.h"

extern struct ledmon_conf conf;

static struct udev_monitor *udev_monitor;

/* Parent of the NVMe multipath namespace-head block devices. */
#define NVME_SUBSYS_VIRT_PATH "/sys/devices/virtual/nvme-subsystem/"

/*
 * Find the tracked device that a resolvable sysfs path refers to. Kernel names
 * are reused, so this compares slot/controller identity rather than names.
 */
static struct block_device *_find_by_block(struct list *block_list, const char *path,
					   struct led_ctx *ctx)
{
	struct block_device *block = NULL;
	struct block_device *bd_new;

	bd_new = block_device_init(sysfs_get_cntrl_devices(ctx), path);
	if (!bd_new)
		return NULL;

	list_for_each(block_list, block) {
		if (block_compare(block, bd_new))
			break;
		block = NULL;
	}
	block_device_fini(bd_new);
	return block;
}

/*
 * An NVMe multipath namespace head links the per-controller devices that
 * ledmon tracks under its multipath/ directory.
 */
static struct block_device *_find_by_mpath(struct list *block_list, const char *syspath,
					   struct led_ctx *ctx)
{
	struct block_device *block = NULL;
	char mpath[PATH_MAX];
	const char *path;
	struct list paths;
	int ret;

	ret = snprintf(mpath, sizeof(mpath), "%s/multipath", syspath);
	if (ret < 0 || ret >= (int)sizeof(mpath))
		return NULL;
	if (scan_dir(mpath, &paths) != 0)
		return NULL;

	list_for_each(&paths, path) {
		block = _find_by_block(block_list, path, ctx);
		if (block)
			break;
	}
	list_erase(&paths);
	return block;
}

/*
 * Last resort: match on the /dev node name. Names are reused, so a stale entry
 * for a drive that has since gone may carry the same name. A remove event is
 * for a drive the last scan saw and an add event for one it did not, so prefer
 * the entry whose presence fits the event.
 */
static struct block_device *_find_by_devnode(struct list *block_list, const char *syspath,
					     bool present)
{
	struct block_device *block, *found = NULL;
	const char *name = strrchr(syspath, '/');

	name = name ? name + 1 : syspath;

	list_for_each(block_list, block) {
		const char *dev_name = strrchr(block->devnode, '/');

		if (!dev_name || strcmp(dev_name + 1, name) != 0)
			continue;
		if ((block->timestamp == timestamp) == present)
			return block;
		if (!found)
			found = block;
	}
	return found;
}

static struct block_device *_find_block(struct list *block_list, const char *syspath,
					enum udev_action act, struct led_ctx *ctx)
{
	struct block_device *block;

	list_for_each(block_list, block) {
		if (strcmp(block->sysfs_path, syspath) == 0)
			return block;
	}

	block = _find_by_block(block_list, syspath, ctx);
	if (block || !is_subpath(syspath, NVME_SUBSYS_VIRT_PATH, strlen(NVME_SUBSYS_VIRT_PATH)))
		return block;

	/*
	 * NVMe multipath events carry the virtual namespace-head path, which
	 * cannot be resolved to a PCI controller, while ledmon tracks the
	 * per-controller device. On add, follow the head's multipath/ links. On
	 * remove the head is already gone from sysfs, so only the name is left.
	 */
	if (act == UDEV_ACTION_ADD) {
		block = _find_by_mpath(block_list, syspath, ctx);
		if (block)
			return block;
	}

	return _find_by_devnode(block_list, syspath, act == UDEV_ACTION_REMOVE);
}

static int create_udev_monitor(void)
{
	int res;
	struct udev *udev = udev_new();

	if (!udev) {
		log_error("Failed to create udev context instance.");
		return -1;
	}

	udev_monitor = udev_monitor_new_from_netlink(udev, "udev");
	if (!udev_monitor) {
		log_error("Failed to create udev monitor object.");
		udev_unref(udev);
		return -1;
	}

	res = udev_monitor_filter_add_match_subsystem_devtype(udev_monitor,
							      "block", "disk");
	if (res < 0) {
		log_error("Failed to modify udev monitor filters.");
		stop_udev_monitor();
		return -1;
	}

	res = udev_monitor_enable_receiving(udev_monitor);
	if (res < 0) {
		log_error("Failed to switch udev monitor to listening mode.");
		stop_udev_monitor();
		return -1;
	}

	return udev_monitor_get_fd(udev_monitor);
}

void stop_udev_monitor(void)
{
	if (udev_monitor) {
		struct udev *udev = udev_monitor_get_udev(udev_monitor);

		udev_monitor_unref(udev_monitor);

		if (udev)
			udev_unref(udev);
	}
}

int get_udev_monitor(void)
{
	if (udev_monitor)
		return udev_monitor_get_fd(udev_monitor);

	return create_udev_monitor();
}

static int _check_raid(const char *path)
{
	const char *t = strrchr(path, '/');

	if (t == NULL)
		return 0;
	return strncmp(t + 1, "md", 2) == 0;
}

static enum udev_action _get_udev_action(const char *action)
{
	enum udev_action ret = UDEV_ACTION_UNKNOWN;

	if (strncmp(action, "add", 3) == 0)
		ret = UDEV_ACTION_ADD;
	else if (strncmp(action, "remove", 6) == 0)
		ret = UDEV_ACTION_REMOVE;
	return ret;
}

static void _clear_raid_dev_info(struct block_device *block, const char *raid_dev)
{
	if (block->raid_dev) {
		char *tmp = strrchr(block->raid_dev->sysfs_path, '/');

		if (tmp == NULL) {
			log_error(
				"Device: %s have wrong raid_dev path: %s",
				block->sysfs_path,
				block->raid_dev->sysfs_path);
			return;
		}
		if (strcmp(raid_dev, tmp + 1) == 0) {
			log_debug(
				"CLEAR raid_dev %s in %s ",
				raid_dev, block->sysfs_path);
			raid_device_fini(block->raid_dev);
			block->raid_dev = NULL;
		}
	}

}

int handle_udev_event(struct list *ledmon_block_list, struct led_ctx *ctx)
{
	struct udev_device *dev;
	int status = -1;

	dev = udev_monitor_receive_device(udev_monitor);
	if (dev) {
		const char *action = udev_device_get_action(dev);
		enum udev_action act = _get_udev_action(action);
		const char *syspath = udev_device_get_syspath(dev);
		struct block_device *block = NULL;

		if (act == UDEV_ACTION_UNKNOWN) {
			status = 1;
			goto exit;
		}

		block = _find_block(ledmon_block_list, syspath, act, ctx);

		if (!block) {
			if (act == UDEV_ACTION_REMOVE && _check_raid(syspath)) {
				/*ledmon is interested about removed arrays*/
				const char *dev_name;

				dev_name = strrchr(syspath, '/') + 1;
				log_debug("REMOVED %s", dev_name);
				list_for_each(ledmon_block_list, block)
					_clear_raid_dev_info(block, dev_name);
				status = 0;
				goto exit;
			}
			status = 1;
			goto exit;
		}

		if (act == UDEV_ACTION_ADD) {
			log_debug("ADDED %s", block->sysfs_path);
			if (block->ibpi == LED_IBPI_PATTERN_FAILED_DRIVE ||
				block->ibpi == LED_IBPI_PATTERN_REMOVED ||
				block->ibpi == LED_IBPI_PATTERN_UNKNOWN)
				block->ibpi = LED_IBPI_PATTERN_ADDED;
		} else if (act == UDEV_ACTION_REMOVE) {
			log_debug("REMOVED %s", block->sysfs_path);
			block->ibpi = LED_IBPI_PATTERN_REMOVED;
		} else {
			/* not interesting event */
			status = 1;
			goto exit;
		}
		status = 0;
	} else {
		return -1;
	}

exit:
	udev_device_unref(dev);
	return status;
}
