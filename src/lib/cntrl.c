// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 Intel Corporation.

/* System headers */
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Public headers */
#include <led/libled.h>

/* Local headers */
#include "amd.h"
#include "cntrl.h"
#include "config.h"
#include "kernel_npem.h"
#include "list.h"
#include "libled_private.h"
#include "npem.h"
#include "smp.h"
#include "sysfs.h"
#include "utils.h"
#include "vmdssd.h"

#if _HAVE_DMALLOC_H
#include <dmalloc.h>
#endif

/**
 */
static int _is_storage_controller(const char *path)
{
	uint64_t class = get_uint64(path, 0, "class");
	return (class & 0xff0000L) == 0x010000L;
}

/**
 */
static int _is_isci_cntrl(const char *path)
{
	return sysfs_check_driver(path, "isci");
}

/**
 */
static int _is_cntrl(const char *path, const char *type)
{
	char temp[PATH_MAX], link[PATH_MAX], *t;

	snprintf(temp, sizeof(temp), "%s/driver", path);

	if (realpath(temp, link) == NULL)
		return 0;

	t = strrchr(link, '/');
	if ((t != NULL) && (strcmp(t + 1, type) != 0))
		return 0;

	return 1;
}

static int _is_ahci_cntrl(const char *path)
{
	return _is_cntrl(path, "ahci");
}

static int _is_nvme_cntrl(const char *path)
{
	return _is_cntrl(path, "nvme");
}

static int _is_intel_ahci_cntrl(const char *path)
{
	if (!_is_ahci_cntrl(path))
		return 0;

	return get_uint64(path, 0, "vendor") == 0x8086L;
}

static int _is_amd_ahci_cntrl(const char *path)
{
	if (!_is_ahci_cntrl(path))
		return 0;

	return get_uint64(path, 0, "vendor") == 0x1022L;
}

static int _is_amd_nvme_cntrl(const char *path)
{
	char tmp[PATH_MAX];
	char *t;

	memset(&tmp, 0, sizeof(tmp));

	if (!_is_nvme_cntrl(path))
		return 0;

	strncpy(tmp, path, PATH_MAX - 1);
	t = strrchr(tmp, '/');
	if (!t)
		return 0;

	t++;
	*t = '\0';
	return get_uint64(tmp, 0, "vendor") == 0x1022L;
}

static int _is_amd_cntrl(const char *path)
{
	if (_is_amd_ahci_cntrl(path))
		return 1;

	if (_is_amd_nvme_cntrl(path))
		return 1;

	return 0;
}

extern int get_dell_server_type(struct led_ctx *ctx);

static int _is_dellssd_cntrl(const char *path, struct led_ctx *ctx)
{
	uint64_t vdr, dev, svdr, cls;
	int gen = 0;

	vdr = get_uint64(path, 0, "vendor");
	dev = get_uint64(path, 0, "device");
	cls = get_uint64(path, 0, "class");
	svdr = get_uint64(path, 0, "subsystem_vendor");
	if (cls == 0x10802)
		gen = get_dell_server_type(ctx);

	return ((vdr == 0x1344L && dev == 0x5150L) || /* micron ssd */
		(gen != 0) ||			      /* Dell Server+NVME */
	        (svdr == 0x1028 && cls == 0x10802));  /* nvmhci ssd */
}

/**
 */
static int _is_smp_cntrl(const char *path)
{
	int result = 0;
	struct list dir;
	char *p;
	char host_path[PATH_MAX] = { 0 };
	if (scan_dir(path, &dir) == 0) {
		const char *dir_path;

		list_for_each(&dir, dir_path) {
			p = strrchr(dir_path, '/');
			if (!p++)
				break;
			if (strncmp(p, "host", strlen("host")) == 0) {
				snprintf(host_path, sizeof(host_path),
					"%s/%s/bsg/sas_%s", path, p, p);
				result = smp_write_gpio(host_path,
					GPIO_REG_TYPE_TX,
					0,
					0,
					"",
					0) == 0;
			}
		}
		list_erase(&dir);
	}

	return result;
}

static int _is_vmd_cntrl(const char *path)
{
	return sysfs_check_driver(path, "vmd");
}

static int _is_npem_cntrl(const char *path, struct led_ctx *ctx)
{
	return is_npem_capable(path, ctx);
}

static int _is_kernel_npem_cntrl(const char *path)
{
	return is_kernel_npem_present(path);
}
/**
 * @brief Determines the type of controller.
 *
 * This is internal function of 'controller device' module. The function
 * determines the type of controller device. It might be AHCI, SCSI or
 * UNKNOWN device type.
 *
 * @param[in]      path           path to controller device in sysfs tree.
 *
 * @return The type of controller device. If the type returned is
 *         CNTRL_TYPE_UNKNOWN this means a controller device is not
 *         supported.
 */
static enum led_cntrl_type _get_type(const char *path, struct led_ctx *ctx)
{
	enum led_cntrl_type type = LED_CNTRL_TYPE_UNKNOWN;

	if (_is_npem_cntrl(path, ctx)) {
		type = LED_CNTRL_TYPE_NPEM;
	} else if (_is_vmd_cntrl(path)) {
		type = LED_CNTRL_TYPE_VMD;
	} else if (_is_kernel_npem_cntrl(path)) {
		type = LED_CNTRL_TYPE_KERNEL_NPEM;
	} else if (_is_dellssd_cntrl(path, ctx)) {
		type = LED_CNTRL_TYPE_DELLSSD;
	} else if (_is_storage_controller(path)) {
		if (_is_intel_ahci_cntrl(path))
			type = LED_CNTRL_TYPE_AHCI;
		else if (_is_amd_cntrl(path))
			type = LED_CNTRL_TYPE_AMD;
		else if (_is_isci_cntrl(path)
				|| sysfs_enclosure_attached_to_cntrl(ctx, path)
				|| _is_smp_cntrl(path))
			type = LED_CNTRL_TYPE_SCSI;
	}
	return type;
}

struct _host_type *alloc_host(int id, struct _host_type *next)
{
	struct _host_type *host = NULL;
	host = malloc(sizeof(struct _host_type));
	if (host) {
		host->host_id = id;
		host->ibpi_state_buffer = NULL;
		memset(host->bitstream, 0, sizeof(host->bitstream));
		host->flush = 0;
		host->ports = 0;
		host->next = next;
	}
	return host;
}

void free_hosts(struct _host_type *h)
{
	struct _host_type *t;
	while (h) {
		t = h->next;
		free(h->ibpi_state_buffer);
		free(h);
		h = t;
	}
}

void _find_host(const char *path, struct _host_type **hosts)
{
	const int host_len = sizeof("host") - 1;
	char *p;
	int index = -1;
	struct _host_type *th;
	DIR *d;
	struct dirent *de;

	p = strrchr(path, '/');
	if (!p++)
		return;
	if (strncmp(p, "host", host_len - 1) == 0) {
		index = atoi(p + host_len);
		th = alloc_host(index, (*hosts) ? (*hosts) : NULL);
		if (!th)
			return;

		d = opendir(path);
		if(!d) {
			free(th);
			return;
		}

		while ((de = readdir(d))) {
			if (strncmp(de->d_name, "phy-", strlen("phy-")) == 0) {
				th->ports++;
			}
		}

		closedir(d);

		*hosts = th;
	}
}

/**
 * @brief Get all instances of separate hosts on isci controller.
 *
 */
static struct _host_type *_cntrl_get_hosts(const char *path)
{
	struct _host_type *hosts = NULL;
	struct list dir;
	if (scan_dir(path, &dir) == 0) {
		const char *dir_path;

		list_for_each(&dir, dir_path)
			_find_host(dir_path, &hosts);
		list_erase(&dir);
	}
	return hosts;
}

/**
 * @brief Check if enclosure management is enabled.
 *
 * This is internal function of 'controller device' module. The function checks
 * whether enclosure management is enabled for AHCI controller.
 *
 * @param[in]      path           path to controller device in sysfs tree.
 *
 * @return 1 if enclosure management is enabled, otherwise the function returns 0.
 */
static unsigned int _ahci_em_messages(const char *path)
{
	/* first, open ...driver/module/parameters/ahci_em_messages
	 * then open /sys/module/libahci/parameters/ahci_em_messages
	 * and check if it is set to enabled.
	 * if so, check if 'holders' of libahci points the same driver name
	 * as device given by path
	 */
	char buf[PATH_MAX];
	char *link, *name;
	DIR *dh;
	struct dirent *de = NULL;

	/* old kernel (prior to 2.6.36) */
	if (get_int(path, 0, "driver/module/parameters/ahci_em_messages") != 0)
		return 1;

	/* parameter type changed from int to bool since kernel v3.13 */
	if (!get_int("", 0, "sys/module/libahci/parameters/ahci_em_messages")) {
		if (!get_bool("", 0, "sys/module/libahci/parameters/ahci_em_messages"))
			return 0;
	}

	if (snprintf(buf, sizeof(buf), "%s/%s", path, "driver") < 0)
		return 0;

	link = realpath(buf, NULL);
	if (!link)
		return 0;

	name = strrchr(link, '/');
	if (!name++) {
		free(link);
		return 0;
	}

	/* check if the directory /sys/module/libahci/holders exists */
	dh = opendir("/sys/module/libahci/holders");
	if (dh) {
		/* name contain controller name (ie. ahci),*/
		/* so check if libahci holds this driver   */
		while ((de = readdir(dh))) {
			if (!strcmp(de->d_name, name))
				break;
		}
		closedir(dh);
		free(link);
		return de ? 1 : 0;
	} else {
		free(link);
		return 1;
	}
}

/*
 * Allocates memory for a new controller device structure. See cntrl.h for
 * details.
 */
struct cntrl_device *cntrl_device_init(const char *path, struct led_ctx *ctx)
{
	unsigned int em_enabled;
	enum led_cntrl_type type;
	struct cntrl_device *device = NULL;

	type = _get_type(path, ctx);
	if (type != LED_CNTRL_TYPE_UNKNOWN) {
		if (!list_is_empty(&ctx->config.allowlist)) {
			char *cntrl = NULL;

			list_for_each(&ctx->config.allowlist, cntrl) {
				if (strcmp(cntrl, path) == 0)
					break;
				cntrl = NULL;
			}
			if (!cntrl) {
				lib_log(ctx, LED_LOG_LEVEL_DEBUG,
					"%s not found on ALLOWLIST, ignoring", path);
				return NULL;
			}
		} else if (!list_is_empty(&ctx->config.excludelist)) {
			char *cntrl;

			list_for_each(&ctx->config.excludelist, cntrl) {
				if (strcmp(cntrl, path) == 0) {
					lib_log(ctx, LED_LOG_LEVEL_DEBUG,
						"%s found on EXCLUDELIST, ignoring", path);
					return NULL;
				}
			}
		}
		switch (type) {
		case LED_CNTRL_TYPE_DELLSSD:
		case LED_CNTRL_TYPE_SCSI:
		case LED_CNTRL_TYPE_VMD:
		case LED_CNTRL_TYPE_NPEM:
		case LED_CNTRL_TYPE_KERNEL_NPEM:
			em_enabled = 1;
			break;
		case LED_CNTRL_TYPE_AHCI:
			em_enabled = _ahci_em_messages(path);
			break;
		case LED_CNTRL_TYPE_AMD:
			em_enabled = amd_em_enabled(path, ctx);
			break;
		default:
			em_enabled = 0;
		}
		if (em_enabled) {
			device = calloc(1, sizeof(struct cntrl_device));
			if (device) {
				if (type == LED_CNTRL_TYPE_SCSI) {
					device->isci_present = _is_isci_cntrl(path);
					device->hosts = _cntrl_get_hosts(path);
				} else {
					device->isci_present = 0;
					device->hosts = NULL;
				}
				if (type == LED_CNTRL_TYPE_VMD) {
					char *domain = vmdssd_get_domain(path);

					if (domain != NULL)
						snprintf(device->domain, PATH_MAX,
							 "%s", domain);
				}
				device->cntrl_type = type;
				strncpy(device->sysfs_path, path, PATH_MAX - 1);
				device->ctx = ctx;
			}
		} else {
			lib_log(ctx, LED_LOG_LEVEL_ERROR,
				"controller discovery: %s - enclosure management not supported.",
				path);
		}
	}
	return device;
}

/*
 * Frees memory allocated for controller device structure. See cntrl.h for
 * details.
 */
void cntrl_device_fini(struct cntrl_device *device)
{
	if (device) {
		free_hosts(device->hosts);
		free(device);
	}
}
