// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022-2023 Red Hat, Inc.

/* LED library */

#include "led/libled.h"

#include <linux/limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "libled_private.h"
#include "list.h"
#include "sysfs.h"
#include "utils.h"
#include "block.h"
#include "slot.h"


led_status_t led_new(struct led_ctx **ctx)
{
	struct led_ctx *t_ctx = calloc(1, sizeof(struct led_ctx));

	if (!t_ctx)
		return LED_STATUS_OUT_OF_MEMORY;

	sysfs_init(t_ctx);

	list_init(&t_ctx->config.allowlist, free);
	list_init(&t_ctx->config.excludelist, free);

	t_ctx->log_fd = -1;
	t_ctx->log_lvl = LED_LOG_LEVEL_ERROR;

	*ctx = t_ctx;
	return LED_STATUS_SUCCESS;
}

led_status_t led_free(struct led_ctx *ctx)
{
	if (!ctx)
		return LED_STATUS_NULL_POINTER;

	amd_sgpio_cache_free(ctx);

	sysfs_reset(ctx);

	list_erase(&ctx->config.allowlist);
	list_erase(&ctx->config.excludelist);

	free(ctx);
	return LED_STATUS_SUCCESS;
}

void led_log_fd_set(struct led_ctx *ctx, int log_fd)
{
	ctx->log_fd = log_fd;
}

void led_log_level_set(struct led_ctx *ctx, enum led_log_level_enum level)
{
	ctx->log_lvl = level;
}

led_status_t led_scan(struct led_ctx *ctx)
{
	if (!ctx)
		return LED_STATUS_NULL_POINTER;

	ctx->deferred_error = LED_STATUS_SUCCESS;

	sysfs_reset(ctx);
	sysfs_scan(ctx);

	return ctx->deferred_error;
}

led_status_t led_device_name_lookup(struct led_ctx *ctx, const char *name, char *result)
{
	struct block_device *device;
	char temp[PATH_MAX];

	if (!realpath(name, temp))
		return LED_STATUS_INVALID_PATH;

	if (is_subpath(temp, SYSTEM_DEV_DIR, strlen(SYSTEM_DEV_DIR)))
		list_for_each(sysfs_get_block_devices(ctx), device)
			if (device->devnode[0] && strncmp(device->devnode, temp, PATH_MAX) == 0) {
				str_cpy(result, device->sysfs_path, PATH_MAX);
				return LED_STATUS_SUCCESS;
			}

	/* Backward compatibility, trust that it is valid sysfs path */
	str_cpy(result, temp, PATH_MAX);
	return LED_STATUS_SUCCESS;
}

enum led_cntrl_type led_is_management_supported(struct led_ctx *ctx, const char *path)
{
	struct block_device *block;

	list_for_each(&ctx->sys.sysfs_block_list, block) {
		if (strcmp(block->sysfs_path, path) == 0)
			return block->cntrl->cntrl_type;
	}
	return LED_CNTRL_TYPE_UNKNOWN;
}

led_status_t led_set(struct led_ctx *ctx, const char *path, enum led_ibpi_pattern ibpi)
{
	struct block_device *device;

	list_for_each(sysfs_get_block_devices(ctx), device) {
		if (strcmp(device->sysfs_path, path) == 0) {
			device->send_message_fn(device, ibpi);
			return LED_STATUS_SUCCESS;
		}
	}
	return LED_STATUS_NOT_SUPPORTED;
}

void led_flush(struct led_ctx *ctx)
{
	struct block_device *device;

	list_for_each(sysfs_get_block_devices(ctx), device) {
		device->flush_message_fn(device);
	}
}

static struct led_slot_list_entry *init_slot(struct slot_property *slot)
{
	struct led_slot_list_entry *s = NULL;

	if (!slot)
		return NULL;

	s = calloc(1, sizeof(*s));

	if (!s)
		return NULL;

	s->slot = slot;

	if (slot->bl_device)
		str_cpy(s->device_name, slot->bl_device->devnode, PATH_MAX);

	return s;
}

static bool slot_compar(void *item1, void *item2)
{
	struct led_slot_list_entry *slot_entry1 = item1;
	struct led_slot_list_entry *slot_entry2 = item2;

	if (strcmp(slot_entry1->slot->slot_id, slot_entry2->slot->slot_id) <= 0)
		return true;
	return false;
}

led_status_t led_slots_get(struct led_ctx *ctx, struct led_slot_list **slots)
{
	led_status_t status;
	struct led_slot_list *rc = NULL;
	struct slot_property *slot;

	if (!slots)
		return LED_STATUS_DATA_ERROR;

	rc = calloc(1, sizeof(struct led_slot_list));
	if (!rc)
		return LED_STATUS_OUT_OF_MEMORY;

	list_init(&rc->slot_list, free);

	list_for_each(sysfs_get_slots(ctx), slot) {
		struct led_slot_list_entry *entry = init_slot(slot);

		if (!entry)
			continue;

		if (!list_insert_compar(&rc->slot_list, entry, slot_compar)) {
			free(entry);
			status = LED_STATUS_OUT_OF_MEMORY;
			goto error;
		}
	}

	*slots = rc;
	return LED_STATUS_SUCCESS;

error:
	led_slot_list_free(rc);
	return status;
}

void led_slot_list_entry_free(struct led_slot_list_entry *se)
{
	free(se);
}

struct led_slot_list_entry *led_slot_find_by_slot(struct led_ctx *ctx, enum led_cntrl_type cntrl,
						  char *slot_id)
{
	return init_slot(find_slot_by_slot_path(ctx, slot_id, cntrl));
}

struct led_slot_list_entry *led_slot_find_by_device_name(struct led_ctx *ctx,
							 enum led_cntrl_type cntrl,
							 char *device_name)
{
	return init_slot(find_slot_by_device_name(ctx, device_name, cntrl));
}

bool led_slot_persistent_id_support(struct led_ctx *ctx, enum led_cntrl_type cntrl)
{
	return cntrl == LED_CNTRL_TYPE_SCSI;
}

led_status_t led_slot_set(struct led_ctx *ctx, struct led_slot_list_entry *se,
				enum led_ibpi_pattern state)
{
	return set_slot_pattern(se->slot, state);
}

bool led_controller_slot_support(enum led_cntrl_type cntrl)
{
	return (cntrl == LED_CNTRL_TYPE_NPEM || cntrl == LED_CNTRL_TYPE_SCSI ||
		cntrl == LED_CNTRL_TYPE_VMD) ||
		cntrl == LED_CNTRL_TYPE_KERNEL_NPEM;
}

struct led_slot_list_entry *led_slot_next(struct led_slot_list *sl)
{
	if (!sl->iter)
		sl->iter = sl->slot_list.head;
	else
		sl->iter = sl->iter->next;

	return (sl->iter) ? sl->iter->item : NULL;
}

struct led_slot_list_entry *led_slot_prev(struct led_slot_list *sl)
{
	if (!sl->iter)
		sl->iter = sl->slot_list.tail;
	else
		sl->iter = sl->iter->prev;

	return (sl->iter) ? sl->iter->item : NULL;
}

const char *led_slot_device(struct led_slot_list_entry *se)
{
	// Device is optional
	size_t len = strnlen(se->device_name, PATH_MAX);

	return (len > 0 && len < PATH_MAX) ? se->device_name : NULL;
}
const char *led_slot_id(struct led_slot_list_entry *se)
{
	return se->slot->slot_id;
}

const char *led_slot_persistent_id(struct led_slot_list_entry *se)
{
	if (se->slot->persistent_id[0] == '\0')
		return NULL;
	return se->slot->persistent_id;
}

enum led_cntrl_type led_slot_cntrl(struct led_slot_list_entry *se)
{
	return se->slot->c->cntrl_type;
}

enum led_ibpi_pattern led_slot_state(struct led_slot_list_entry *se)
{
	return get_slot_pattern(se->slot);
}

void led_slot_list_free(struct led_slot_list *sl)
{
	if (sl) {
		list_erase(&sl->slot_list);
		sl->iter = NULL;
		free(sl);
	}
}

void led_slot_list_reset(struct led_slot_list *sl)
{
	if (sl)
		sl->iter = NULL;
}

/**
 * @brief Name of controllers types.
 *
 * This is internal array with names of controller types. Array can be use to
 * translate enumeration type into the string.
 */
static const char * const ctrl_type_str[] = {
	[LED_CNTRL_TYPE_UNKNOWN] = "?",
	[LED_CNTRL_TYPE_DELLSSD] = "Dell SSD",
	[LED_CNTRL_TYPE_VMD]     = "VMD",
	[LED_CNTRL_TYPE_SCSI]    = "SCSI",
	[LED_CNTRL_TYPE_AHCI]    = "AHCI",
	[LED_CNTRL_TYPE_NPEM]    = "NPEM",
	[LED_CNTRL_TYPE_AMD]     = "AMD",
	[LED_CNTRL_TYPE_KERNEL_NPEM]     = "Kernel NPEM",
};

enum led_cntrl_type led_string_to_cntrl_type(const char *cntrl_str)
{
	for (int i = 0; i < ARRAY_SIZE(ctrl_type_str); i++) {
		if (strcasecmp(cntrl_str, ctrl_type_str[i]) == 0)
			return (enum led_cntrl_type)i;
	}
	return LED_CNTRL_TYPE_UNKNOWN;
}

const char * const led_cntrl_type_to_string(enum led_cntrl_type cntrl)
{
	return ctrl_type_str[cntrl];
}

static led_status_t get_cntrl(struct cntrl_device *cntrl_dev, struct led_cntrl_list *cl)
{
	struct led_cntrl_list_entry *cntrl = calloc(1, sizeof(struct led_cntrl_list_entry));

	if (!cntrl) {
		led_cntrl_list_free(cl);
		return LED_STATUS_OUT_OF_MEMORY;
	}

	snprintf(cntrl->path, PATH_MAX, "%s", cntrl_dev->sysfs_path);
	cntrl->cntrl_type = cntrl_dev->cntrl_type;
	if (!list_append(&cl->cntrl_list, cntrl)) {
		free(cntrl);
		led_cntrl_list_free(cl);
		return LED_STATUS_OUT_OF_MEMORY;
	}
	return LED_STATUS_SUCCESS;
}

led_status_t led_cntrls_get(struct led_ctx *ctx, struct led_cntrl_list **cntrls)
{
	led_status_t status;
	struct cntrl_device *ctrl_dev;

	struct led_cntrl_list *rc = calloc(1, sizeof(struct led_cntrl_list));

	if (!rc)
		return LED_STATUS_OUT_OF_MEMORY;

	list_init(&rc->cntrl_list, free);

	list_for_each(sysfs_get_cntrl_devices(ctx), ctrl_dev) {
		status = get_cntrl(ctrl_dev, rc);
		if (status != LED_STATUS_SUCCESS)
			return status;
	}

	*cntrls = rc;
	return LED_STATUS_SUCCESS;
}

void led_cntrl_list_reset(struct led_cntrl_list *cl)
{
	if (cl)
		cl->iter = NULL;
}

struct led_cntrl_list_entry *led_cntrl_next(struct led_cntrl_list *cl)
{
	if (!cl->iter)
		cl->iter = cl->cntrl_list.head;
	else
		cl->iter = cl->iter->next;

	return (cl->iter) ? cl->iter->item : NULL;
}

struct led_cntrl_list_entry *led_cntrl_prev(struct led_cntrl_list *cl)
{
	if (!cl->iter)
		cl->iter = cl->cntrl_list.tail;
	else
		cl->iter = cl->iter->prev;

	return (cl->iter) ? cl->iter->item : NULL;
}

const char *led_cntrl_path(struct led_cntrl_list_entry *c)
{
	return c->path;
}

enum led_cntrl_type led_cntrl_type(struct led_cntrl_list_entry *c)
{
	return c->cntrl_type;
}

void led_cntrl_list_free(struct led_cntrl_list *cntrls)
{
	if (cntrls) {
		list_erase(&cntrls->cntrl_list);
		cntrls->iter = NULL;
		free(cntrls);
	}
}

