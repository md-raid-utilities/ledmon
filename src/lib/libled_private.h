// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022-2023 Red Hat, Inc.

/* LED library private */

#ifndef _LIBLED_PRIVATE_H_INCLUDED_
#define _LIBLED_PRIVATE_H_INCLUDED_

/* System headers */
#include <linux/limits.h>

/* Public headers */
#include <led/libled.h>

/* Local headers */
#include "amd_sgpio.h"
#include "list.h"
#include "libled_internal.h"
#include "slot.h"
#include "sysfs.h"

/**
 * @brief Embedded structure within the library context used for amd state and caching.
 */
struct amd_sgpio_state {
	int cache_fd;
	struct cache_entry *cache;
};

/**
 * @brief Configuration options for the library.  Mainly used by ledmon.
 */
struct configuration {
	int blink_on_migration;
	int blink_on_init;
	int rebuild_blink_on_all;
	int raid_members_only;
	int userspace_npem;

	struct list allowlist;
	struct list excludelist;
};

/**
 * @brief Implementation of the opaque led_slot_list_entry.
 */
struct led_slot_list_entry {
	struct slot_property *slot;
	char device_name[PATH_MAX];
};

/**
 * @brief Implementation of the opaque led_slot_list.
 */
struct led_slot_list {
	struct list slot_list;
	struct node *iter;
};

/**
 * @brief Implementation of the opaque led_cntrl_list_entry.
 */
struct led_cntrl_list_entry {
	char path[PATH_MAX];
	enum led_cntrl_type cntrl_type;
};

/**
 * @brief Implementation of the opaque led_cntrl_list.
 */
struct led_cntrl_list {
	struct list cntrl_list;
	struct node *iter;
};

/**
 * @brief Library context
 *
 * This structure is the common data for the library context.  This is an opaque data type and thus
 * the internals of this structure cannot be visible to library users.
 */
struct led_ctx {
	struct sysfs sys;
	int log_fd;
	enum led_log_level_enum log_lvl;
	led_status_t deferred_error;
	int dellssd_hw_gen;	/* cached value for dellssd hardware generation */
	long ipmi_msgid;	/* incrementing message id */
	struct amd_sgpio_state amd_sgpio;
	struct configuration config;

	struct led_cntrl_list cl;
	struct led_slot_list sl;
};

#endif /* _LIBLED_PRIVATE_H_INCLUDED_ */
