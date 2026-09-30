// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 Intel Corporation.

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "config.h"

#if _HAVE_DMALLOC_H
#include <dmalloc.h>
#endif

#include "list.h"
#include "sysfs.h"
#include "tail.h"
#include "utils.h"

/**
 */
static unsigned char _get_state(const char *path)
{
	char *p, *t, *s;
	unsigned char result = TAIL_STATE_UNKNOWN;

	s = p = get_text(path, "state");
	if (p) {
		while (s) {
			t = strchr(s, ',');
			if (t)
				*(t++) = '\0';
			if (strcmp(s, "spare") == 0)
				result |= TAIL_STATE_SPARE;
			else if (strcmp(s, "in_sync") == 0)
				result |= TAIL_STATE_IN_SYNC;
			else if (strcmp(s, "faulty") == 0)
				result |= TAIL_STATE_FAULTY;
			else if (strcmp(s, "write_mostly") == 0)
				result |= TAIL_STATE_WRITE_MOSTLY;
			else if (strcmp(s, "blocked") == 0)
				result |= TAIL_STATE_BLOCKED;
			s = t;
		}
		free(p);
	}
	return result;
}

/**
 */
static unsigned int _get_errors(const char *path)
{
	return get_int(path, 0, "errors");
}

/**
 */
static int _get_slot(const char *path, unsigned int *dest)
{
	int ret = 1;
	unsigned int n;
	char *p = get_text(path, "slot");

	if (p) {
		if (strcmp(p, "none") != 0)
			if (str_toui(&n, p, NULL, 10) == 0) {
				*dest = n;
				ret = 0;
			}
		free(p);
	}
	return ret;
}

/**
 * @brief Determine whether a tracked block device backs a RAID member.
 *
 * @param[in]  block    Block device tracked by ledmon.
 * @param[in]  syspath  Canonicalized sysfs path of the block device backing
 *                      the RAID member (the md .../dev-XXX/block symlink,
 *                      resolved).
 *
 * Normally a member's backing device resolves to exactly the sysfs path that
 * ledmon tracks, so a path comparison is sufficient. NVMe multipath members
 * are the exception: they resolve to a virtual namespace-head path (e.g.
 * /sys/devices/virtual/nvme-subsystem/nvme-subsysN/nvme2n1) while ledmon
 * tracks the underlying per-controller device (nvme2c2n1) by its physical PCI
 * path. Their sysfs paths differ, but both share the same /dev node, so fall
 * back to matching on the device node. Without this fallback array-derived
 * states (REBUILD, DEGRADED, in-array FAILURE) are never applied to multipath
 * NVMe drives.
 *
 * @return true if @p block backs @p syspath, otherwise false.
 */
bool tail_block_matches(const struct block_device *block, const char *syspath)
{
	char devnode[PATH_MAX];
	const char *name;
	int ret;

	if (strcmp(block->sysfs_path, syspath) == 0)
		return true;

	if (block->devnode[0] == '\0')
		return false;

	name = strrchr(syspath, '/');
	name = name ? name + 1 : syspath;
	if (name[0] == '\0')
		return false;

	ret = snprintf(devnode, sizeof(devnode), SYSTEM_DEV_DIR "/%s", name);
	if (ret < 0 || ret >= (int)sizeof(devnode))
		return false;

	return strcmp(block->devnode, devnode) == 0;
}

/**
 */
static struct block_device *_get_block(const char *path, struct list *block_list)
{
	char temp[PATH_MAX];
	char link[PATH_MAX];
	struct block_device *device;

	snprintf(temp, sizeof(temp), "%s/block", path);

	if (!realpath(temp, link))
		return NULL;

	/* translate partition to master block dev */
	if (snprintf(temp, PATH_MAX, "%s/partition", link) > 0) {
		struct stat sb;
		char *ptr;

		if (stat(temp, &sb) == 0 && S_ISREG(sb.st_mode)) {
			ptr = strrchr(link, '/');
			if (ptr)
				*ptr = '\0';
		}
	}

	list_for_each(block_list, device) {
		if (tail_block_matches(device, link))
			return device;
	}
	return NULL;
}

/**
 */
struct tail_device *tail_device_init(const char *path, struct list *block_list)
{
	struct tail_device *device = NULL;
	struct block_device *block;

	block = _get_block(path, block_list);
	if (block) {
		device = malloc(sizeof(struct tail_device));
		if (device && _get_slot(path, &device->slot) == 0) {
			device->raid = NULL;
			device->state = _get_state(path);
			device->errors = _get_errors(path);
			device->block = block;
		} else {
			free(device);
			device = NULL;
		}
	}
	return device;
}

/**
 */
void tail_device_fini(struct tail_device *device)
{
	free(device);
}
