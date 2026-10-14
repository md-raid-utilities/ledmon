// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2023 Intel Corporation.

#ifndef _SLOT_H_INCLUDED_
#define _SLOT_H_INCLUDED_

/* Project headers */
#include <led/libled.h>

/* Local headers */
#include "block.h"
#include "cntrl.h"

/* Forward decl. */
struct slot_property;


/**
 * @brief slot property attributes which are the same across slots of a specific type
 *
 */
struct slot_property_common {
	/**
	 * Controller type which is being represented by slot.
	 */
	enum led_cntrl_type cntrl_type;

	/**
	 * Pointer to the set slot function.
	 */
	status_t (*set_slot_fn)(struct slot_property *slot, enum led_ibpi_pattern state);

	/**
	 * Pointer to the get led state function.
	 */
	enum led_ibpi_pattern (*get_state_fn)(struct slot_property *slot);
};

/**
 * @brief Encapsulates information for enclosure
 *
 */
struct ses_slot_info {
	struct enclosure_device *encl;
	int slot_num;
};

struct amd_sgpio_slot {
	char em_buffer_path[PATH_MAX];
	int ata_port;
	int port;
	int drive_bay;
	int initiator;
	struct led_ctx *ctx;
};


/**
 * @brief slot property parameters
 *
 * This structure contains slot parameters.
 */
struct slot_property {
	const struct slot_property_common *c;

	/**
	 * Block device (if exists for slot).
	 */
	struct block_device *bl_device;

	/**
	 * Different payload based on slot type
	 */
	union {
		struct pci_slot *pci;
		struct cntrl_device *cntrl;
		struct ses_slot_info ses;
		struct amd_sgpio_slot amd_sgpio;
	} slot_spec;

	/**
	 * Unique slot ID.
	 */
	char slot_id[PATH_MAX];

	/**
	 * @brief Persistent ID for the slot.
	 *
	 * This string that is the persistent ID for the slot.
	 * It is used to identify the slot across reboots. Empty string if not supported.
	 */
	char persistent_id[PATH_MAX];

};

/**
 * @brief Find slot device by path to slot.
 *
 * @param[in]        ctx              The library context.
 * @param[in]        slot_path        Path to slot.
 * @param[in]        cntrl_type       Type of controller.
 *
 * @return This function returns related slot property.
 */
struct slot_property *find_slot_by_slot_path(struct led_ctx *ctx, char *slot_path,
					     enum led_cntrl_type cntrl_type);

/**
 * @brief Find slot device by device name.
 *
 * @param[in]        ctx              The library context.
 * @param[in]        device_name       Device name.
 * @param[in]        cntrl_type       Type of controller.
 *
 * @return This function returns related slot property.
 */
struct slot_property *find_slot_by_device_name(struct led_ctx *ctx, char *device_name,
					       enum led_cntrl_type cntrl_type);

/**
 * @brief Set the slot pattern for the given slot
 *
 * @param[in]      slot       The slot to change the ibpi_pattern
 * @param[in]      state      The desired ibpi pattern for the slot
 * @return status_t
 */
status_t set_slot_pattern(struct slot_property *slot, enum led_ibpi_pattern state);

/**
 * @brief Get the slot pattern
 *
 * @param[in]      slot        The slot to retrieve the ibpi pattern
 * @return enum ibpi_pattern
 */
enum led_ibpi_pattern get_slot_pattern(struct slot_property *slot);

#endif // SLOT_H_INCLUDED_
