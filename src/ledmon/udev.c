// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 Intel Corporation.

/* System headers */
#include <errno.h>
#include <limits.h>
#include <libudev.h>
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

static int _compare(const struct block_device *bd, const char *syspath, struct led_ctx *ctx)
{
	struct block_device *bd_new;
	const char *udev_name;
	const char *dev_name;
	int ret;

	if (!bd || !syspath)
		return 0;

	if (strcmp(bd->sysfs_path, syspath) == 0)
		return 1;

	bd_new = block_device_init(sysfs_get_cntrl_devices(ctx), syspath);
	if (bd_new) {
		ret = block_compare(bd, bd_new);
		block_device_fini(bd_new);
		if (ret)
			return 1;
	}

	/*
	 * NVMe udev events may arrive with a virtual nvme-subsystem path
	 * that cannot be resolved to a PCI controller. Fall back to matching
	 * by device name so that add/remove events are not silently dropped.
	 */
	if (bd->devnode[0] == '\0')
		return 0;

	dev_name = strrchr(bd->devnode, '/');
	dev_name = dev_name ? dev_name + 1 : bd->devnode;

	udev_name = strrchr(syspath, '/');
	udev_name = udev_name ? udev_name + 1 : syspath;

	return strcmp(dev_name, udev_name) == 0;
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
	char *t = strrchr(path, '/');

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

static void _clear_raid_dev_info(struct block_device *block, char *raid_dev)
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

		list_for_each(ledmon_block_list, block) {
			if (_compare(block, syspath, ctx))
				break;
			block = NULL;
		}

		if (!block) {
			if (act == UDEV_ACTION_REMOVE && _check_raid(syspath)) {
				/*ledmon is interested about removed arrays*/
				char *dev_name;

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
