// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 Intel Corporation.

#ifndef _BLOCK_H_INCLUDED_
#define _BLOCK_H_INCLUDED_

/* System headers */
#include <stdbool.h>
#include <time.h>

/* Public headers */
#include <led/libled.h>

/* Local headers */
#include "cntrl.h"
#include "list.h"
#include "raid.h"
#include "status.h"

struct block_device;

/**
 * @brief Describes a block device.
 *
 * This structure describes a block device. It does not describe virtual devices
 * or partitions on physical block devices.
 */
struct block_device {
/**
 * Real path in sysfs tree. This means i.e. if /sys/block/sda is symbolic link
 * then the link will be read and path stored in sysfs_path field. This path
 * may not exist in sysfs if connection to physical drive is lost. This filed
 * cannot have NULL pointer assigned.
 */
	char sysfs_path[PATH_MAX];

/**
 * Main devnode we can reach this device. It may not match /sys/dev/block/MAJ:MIN
 * Could be empty.
 */
	char devnode[PATH_MAX];

/**
 * The pointer to a function which sends a message to driver in order to
 * control LEDs in an enclosure or DAS system.
 * Params:
 *	device - pointer to a block device.
 *	ibpi - an IBPI pattern (state) to visualize.
 * Return: STATUS_SUCCESS if successful, otherwise a valid status_t status code.
 */
	status_t (*send_message_fn)(struct block_device *device, enum led_ibpi_pattern ibpi);

/**
 * The pointer to a function which flush buffers filled by send_message_fn.
 * Params:
 *	device - pointer to a block device.
 * Return 1 if successful, otherwise the function returns 0.
 */
	int (*flush_message_fn)(struct block_device *device);

/**
 * Canonical path to block device where enclosure management fields are located.
 * This path is always accessible even if the connection to physical device
 * is lost. In case of AHCI controller it points to SATA phy. In case of SAS
 * this path points to SES entry associated with the slot in an enclosure.
 * This field cannot have NULL pointer assign.
 */
	char cntrl_path[PATH_MAX];

/**
 * The current state of block device. This is an IBPI pattern and it is used
 * to visualize the state of block device.
 */
	enum led_ibpi_pattern ibpi;

/**
 * The previous state of block device.
 */
	enum led_ibpi_pattern ibpi_prev;

/**
 * The time stamp used to determine if the given block device still exist or
 * it failed and the device is no longer available. Every time IBPI pattern
 * is updated, the time-stamp is updated, too.
 */
	time_t timestamp;

/**
 * The pointer to storage controller structure the device is connected to.
 */
	struct cntrl_device *cntrl;

	struct _host_type *host;

	int host_id;

/**
 * The index of phy utilized by directly attached to controller block device.
 * It is meaningful if device is controlled by isci driver.
 */
	int phy_index;

/**
 * The index in Enclosure. This is what should be used when using SES-2.
 */
	int encl_index;

	struct enclosure_device *enclosure;

/**
 * If disk is a raid member, this field will be set with a copy of raid device
 * struct.
 */
	struct raid_device *raid_dev;
};

/**
 * @brief Creates a block device structure.
 *
 * This function allocates memory for a new structure of block device. It reads
 * the sysfs entries and populates the structure fields. It performs all this
 * actions only if the block device is connected to the one of supported storage
 * controllers and the controller has enclosure management services enabled.
 *
 * @param[in]      cntrl_list     pointer to a list of supported controller
 *                                devices.
 * @param[in]      sysfs_path     a path to block device in sysfs.
 *
 * @return Pointer to block device structure if successful, otherwise the function
 *         returns the NULL pointer.
 */
struct block_device *block_device_init(const struct list *cntrl_list, const char *path);

/**
 * @brief Releases a block device structure.
 *
 * This function releases memory allocated for block device structure.
 *
 * @param[in]      device         pointer to block device structure.
 *
 * @return The function does not return a value.
 */
void block_device_fini(struct block_device *device);

/**
 * @brief Duplicates a block device structure.
 *
 * The function allocates memory for a block device structure and copies values
 * stored in fields of source block structure. The function allocates new memory
 * for all string fields in a copy structure. It is safe to release source block
 * structure just after it has been duplicated.
 *
 * @param[in]      device         pointer to source block device structure.
 *
 * @return Pointer to block device structure if successful, otherwise the function
 *         returns the NULL pointer.
 */
struct block_device *block_device_duplicate(struct block_device *device);

/**
 * @brief Determines a storage controller.
 *
 * This is the internal function of 'block device' module. The function gets
 * a pointer to controller structure the device is connected to.
 *
 * @param[in]      cntrl_list     pointer to list of supported controllers.
 * @param[in]      path           path to block device in sysfs tree.
 *
 * @return Pointer to controller structure if successful, otherwise the function
 *         returns NULL pointer. The NULL pointer means that block devices is
 *         connected to unsupported storage controller.
 */
struct cntrl_device *block_get_controller(const struct list *cntrl_list, char *path);

/**
 * The global timestamp variable. It is updated every time the sysfs is scanning
 * by an application. The value stored in this variable should be used to update
 * all timestamp stored in block device structures.
 */
extern time_t timestamp;

/**
 * @brief Determines if block device is attached directly or via expander
 */
int dev_directly_attached(const char *path);

/**
 * @brief Gets the host structure for given control device and host_id
 */
struct _host_type *block_get_host(struct cntrl_device *cntrl, int host_id);


/**
 * @brief Checks the presence of block device.
 *
 * This is internal function of monitor service. The function is checking
 * whether block device is already on the list or it is missing from the list.
 * The function is design to be used as 'test' parameter for list_find_first()
 * function.
 *
 * @param[in]    bd_old          - an element from a list to compare to.
 * @param[in]    bd_new          - a block device being searched.
 *
 * @return 0 if the block devices do not match, otherwise function returns 1.
 */
int block_compare(const struct block_device *bd_old,
		  const struct block_device *bd_new);

/**
 * @brief Checks whether a sysfs path contains a sub-path.
 *
 * The match must start at a path component, so a PCI slot address such as
 * "0000:01:00" does not match inside "10000:01:00.0" (a VMD domain). Unless
 * sub_path_to_end is set, the character after the match is required to be one
 * of the following ('\n', '\0', '/'), otherwise it is excluded.
 *
 * @param[in]        path                Sysfs path to search.
 * @param[in]        sub_path            Sub path.
 * @param[in]        sub_path_to_end     True if sub_path is complete path.
 *
 * @return true if path contains sub_path, otherwise false.
 */
bool block_path_has_subpath(const char *path, const char *sub_path, bool sub_path_to_end);

/**
 * @brief Finds block device which name contains sub-path.
 *
 * This function scans block devices and checks their sysfs path
 * to find any which contains PCI address specified for device in path.
 * See block_path_has_subpath() for the matching rules.
 *
 * @param[in]        ctx                 Library context.
 * @param[in]        sub_path            Sub path.
 * @param[in]        sub_path_to_end     True if sub_path is complete path.
 *
 * @return first block device containing sub-path if any, otherwise NULL.
 */
struct block_device *get_block_device_from_sysfs_path(struct led_ctx *ctx, char *sub_path,
						      bool sub_path_to_end);

#endif				/* _BLOCK_H_INCLUDED_ */
