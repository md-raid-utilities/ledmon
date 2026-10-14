// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2023, Advanced Micro Devices, Inc.

/* AMD SGPIO LED control */

#ifndef _AMD_SGPIO_INCLUDED_
#define _AMD_SGPIO_INCLUDED_

#include <stdint.h>

#include "block.h"
#include "cntrl.h"
#include "list.h"
#include "led/libled.h"

typedef uint8_t drive_led_t;

struct drive_leds {
	drive_led_t	error;
	drive_led_t	locate;
	drive_led_t	activity;
} __attribute__ ((__packed__));

struct cache_entry {
	struct drive_leds leds[4];
	uint8_t blink_gen_a;
	uint8_t blink_gen_b;
	uint16_t reserved;
} __attribute__ ((__packed__));

int _amd_sgpio_em_enabled(const char *path, struct led_ctx *ctx);
status_t _amd_sgpio_write(struct block_device *device, enum led_ibpi_pattern ibpi);
char *_amd_sgpio_get_path(const char *cntrl_path, struct led_ctx *ctx);
void amd_sgpio_slots_add(struct cntrl_device *cntrl, struct list *slots);

void amd_sgpio_cache_free(struct led_ctx *ctx);

#endif
