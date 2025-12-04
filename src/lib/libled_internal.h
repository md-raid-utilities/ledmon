// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022-2023 Red Hat, Inc.

/* LED library internal */

/*
 * NOTE: This are functions that are used by ledctl and ledmon,
 * but are not part of the public API
 */

#ifndef _LIB_LED_INTERNAL_INCLUDED_
#define _LIB_LED_INTERNAL_INCLUDED_

#include "led/libled.h"

/**
 * @brief Turns off all LEDs for all support hardware for the system.
 *
 * @param[in] ctx	Library context.
 *
 * Note: Needs to be followed with led_flush().
 */
void off_all(struct led_ctx *ctx);

/**
 * @brief Sets library blink behavior.
 *
 * @param[in]	ctx		Library context.
 * @param[in]	migration	Migration behavior.
 * @param[in]	init		Init behavior.
 * @param[in]	all		All led behavior.
 * @param[in]	raid_members	Raid member behavior.
 *
 * @return led_status_t LED_STATUS_SUCCESS on success, else error reason.
 */
led_status_t device_blink_behavior_set(struct led_ctx *ctx, int migration,
						int init, int all, int raid_members);

/**
 * @brief Add a device path to an allow list.  The library will then only consider this device
 * for API calls.  Function can be repeated, to add to an internal list.
 *
 * @param[in]	ctx	Library context.
 * @param]in]	path	Device path in allow.
 *
 * @return led_status_t	LED_STATUS_SUCCESS on success, else error reason.
 *
 * Note: Mutually exclusive with exclude, allow takes priority.
 */
led_status_t device_allow_pattern_add(struct led_ctx *ctx, const char *path);

/**
 * @brief Add a device path to an exclude list.  The library will then exclude this device
 * from API calls.  Function can be repeated, to add to an internal list.
 *
 * @param[in]	ctx	Library context.
 * @param[in]	path	Device path to exclude.
 *
 * @return led_status_t LED_STATUS_SUCCESS on success, else error reason.
 */
led_status_t device_exclude_pattern_add(struct led_ctx *ctx, const char *path);

/**
 * @brief Log to the library context fd with the specified log level and message
 *
 * @param[in]	ctx		Library context
 * @param[in]	loglevel	Enumerated log level
 * @param[in]	buf		printf style format string
 * @param[in]	...		Variable number of arguments for printf format string
 */
void lib_log(struct led_ctx *ctx, enum led_log_level_enum loglevel, const char *buf, ...)
				__attribute__ ((format (printf, 3, 4)));

/*
 * Ensure GNU basename behavior on GLIBC less systems.
 */
#ifndef __GLIBC__
#define basename(path) \
	(strrchr((path), '/') ? strrchr((path), '/') + 1 : (path))
#endif

#endif
