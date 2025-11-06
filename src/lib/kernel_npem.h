// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 Intel Corporation.
// Copyright (C) 2025 Dell Inc.

#ifndef _KERNEL_NPEM_H_INCLUDED_
#define _KERNEL_NPEM_H_INCLUDED_

/* Project headers */
#include <led/libled.h>

/* Local headers */
#include "block.h"
#include "cntrl.h"
#include "slot.h"
#include "status.h"


int is_kernel_npem_present(const char *path);
status_t kernel_npem_write(struct block_device *device, enum led_ibpi_pattern ibpi);
char *kernel_npem_get_path(const char *cntrl_path);

/**
 * @brief Gets slot information.
 *
 * This function fills slot information related to the slot.
 *
 * @param[in]         slot       Pointer to the slot property element.
 *
 * @return STATUS_SUCCESS if successful, otherwise a valid status_t status code.
 */
enum led_ibpi_pattern kernel_npem_get_state(struct slot_property *slot);

/**
 * @brief Sets led state for slot.
 *
 * This function sets given led state for slot.
 *
 * @param[in]         slot           Requested slot.
 * @param[in]         state          IBPI state based on slot request.
 *
 * @return STATUS_SUCCESS if successful, otherwise a valid status_t status code.
 */
status_t kernel_npem_set_state(struct slot_property *slot, enum led_ibpi_pattern state);

/**
 * @brief Initializes a slot_property for a specified NPEM controller.
 *
 * @param[in]         npem_cntrl       Specified npem controller for this slot
 * @return struct slot_property* if successful, else NULL on allocation failure
 */
struct slot_property *kernel_npem_slot_property_init(struct cntrl_device *npem_cntrl);

#endif // _KERNEL_NPEM_H_INCLUDED_
