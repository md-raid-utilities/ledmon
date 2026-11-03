// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 Intel Corporation.

/* System headers */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <uuid/uuid.h>
#include <fcntl.h>
#include <unistd.h>
/* Local headers */
#include "list.h"
#include "libled_internal.h"
#include "pci_slot.h"
#include "status.h"
#include "sysfs.h"
#include "utils.h"
#include "vmdssd.h"

#define ATTENTION_OFF        0xF  /* (1111) Attention Off, Power Off */
#define ATTENTION_LOCATE     0x7  /* (0111) Attention Off, Power On */
#define ATTENTION_REBUILD    0x5  /* (0101) Attention On, Power On */
#define ATTENTION_FAILURE    0xD  /* (1101) Attention On, Power Off */

struct ibpi2value ibpi_to_attention[] = {
	{LED_IBPI_PATTERN_NORMAL, ATTENTION_OFF},
	{LED_IBPI_PATTERN_LOCATE, ATTENTION_LOCATE},
	{LED_IBPI_PATTERN_FAILED_DRIVE, ATTENTION_FAILURE},
	{LED_IBPI_PATTERN_REBUILD, ATTENTION_REBUILD},
	{LED_IBPI_PATTERN_LOCATE_OFF, ATTENTION_OFF},
	{LED_IBPI_PATTERN_ONESHOT_NORMAL, ATTENTION_OFF},
	{LED_IBPI_PATTERN_UNKNOWN, 0}
};

#define SYSFS_PCIEHP         "/sys/module/pciehp"
#define SYSFS_VMD            "/sys/bus/pci/drivers/vmd"

/* VMD LED control ACPI DSM UUID (Intel VMD SSD LED interface) */
static const char *VMD_LED_DSM_UUID = "6adb7146-32b8-4f9b-b771-18df1f420618";

#define ACPI_DSM_DEVICE "/dev/vmd_dsm"
#define VMD_DSM_IOCTL _IOW('A', 0x01, struct vmd_dsm_request)
struct vmd_dsm_request {
    char acpi_path[64];
    uuid_t guid;
    uint64_t rev;
    uint64_t func;
    uint64_t arg;
};

static status_t vmdssd_acpi_led_control(struct block_device *device, enum led_ibpi_pattern ibpi);
static status_t vmdssd_acpi_dsm_call(const char *guid_str, uint64_t function, uint64_t arg);

static char *get_slot_from_syspath(const char *path)
{
	char *cur, *ret = NULL;
	char *temp_path = strdup(path);

	if (!temp_path)
		return NULL;

	cur = strtok(temp_path, "/");
	while (cur != NULL) {
		char *next = strtok(NULL, "/");

		if ((next != NULL) && strcmp(next, "nvme") == 0)
			break;
		cur = next;
	}

	cur = strtok(cur, ".");
	if (cur)
		ret = strdup(cur);
	free(temp_path);

	return ret;
}

char *vmdssd_get_domain(const char *path)
{
	char domain_path[PATH_MAX], real_domain_path[PATH_MAX];
	char *tok;

	snprintf(domain_path, PATH_MAX, "%s/%s/domain",
		 SYSFS_VMD, basename(path));
	if (realpath(domain_path, real_domain_path) == NULL)
		return NULL;

	tok = strtok(basename(real_domain_path), ":");
	if (!tok)
		return NULL;

	return strdup(tok);
}

bool vmdssd_check_slot_module(struct led_ctx *ctx, const char *slot_path)
{
	char *address;
	struct cntrl_device *cntrl;

	address = get_text(slot_path, "address");
	if (address == NULL)
		return false;

	// check if slot address contains vmd domain
	list_for_each(sysfs_get_cntrl_devices(ctx), cntrl) {
		if (cntrl->cntrl_type == LED_CNTRL_TYPE_VMD) {
			if (cntrl->domain[0] == '\0')
				continue;
			if (strstr(address, cntrl->domain) == NULL)
				continue;
			free(address);
			return true;
		}
	}
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, "vmdssd_check_slot_module: %d FAILED", __LINE__);
	free(address);
	return false;
}

struct pci_slot *vmdssd_find_pci_slot(struct led_ctx *ctx, const char *device_path)
{
	char *pci_addr;
	struct pci_slot *slot = NULL;

	pci_addr = get_slot_from_syspath(device_path);
	if (!pci_addr)
		return NULL;

	list_for_each(sysfs_get_pci_slots(ctx), slot) {
		if (strcmp(slot->address, pci_addr) == 0)
			break;
		slot = NULL;
	}
	free(pci_addr);
	if (slot == NULL || !vmdssd_check_slot_module(ctx, slot->sysfs_path))
		return NULL;

	return slot;
}

enum led_ibpi_pattern vmdssd_get_attention(struct pci_slot *slot)
{
	int attention = get_int(slot->sysfs_path, -1, "attention");
	const struct ibpi2value *ibpi2val;

	if (attention == -1)
		return LED_IBPI_PATTERN_UNKNOWN;

	ibpi2val = get_by_value(attention, ibpi_to_attention, ARRAY_SIZE(ibpi_to_attention));
	return ibpi2val->ibpi;
}

status_t vmdssd_write_attention_buf(struct pci_slot *slot, enum led_ibpi_pattern ibpi)
{
	char attention_path[PATH_MAX + strlen("/attention")];
	char buf[WRITE_BUFFER_SIZE];
	const struct ibpi2value *ibpi2val;

	uint16_t val;

	lib_log(slot->ctx, LED_LOG_LEVEL_DEBUG,
		"%s before: 0x%x\n", slot->address,
		(unsigned int)get_int(slot->sysfs_path, 0, "attention"));

	ibpi2val = get_by_ibpi(ibpi, ibpi_to_attention, ARRAY_SIZE(ibpi_to_attention));
	if (ibpi2val->ibpi == LED_IBPI_PATTERN_UNKNOWN) {

		lib_log(slot->ctx, LED_LOG_LEVEL_INFO,
			"VMD: Controller doesn't support %s pattern\n", ibpi2str(ibpi));
		return STATUS_INVALID_STATE;
	}
	val = (uint16_t)ibpi2val->value;

	snprintf(buf, WRITE_BUFFER_SIZE, "%u", val);
	snprintf(attention_path, sizeof(attention_path), "%s/attention", slot->sysfs_path);
	if (buf_write(attention_path, buf) != (ssize_t) strnlen(buf, WRITE_BUFFER_SIZE)) {
		lib_log(slot->ctx, LED_LOG_LEVEL_ERROR,
			"%s write error: %d\n", slot->sysfs_path, errno);
		return STATUS_FILE_WRITE_ERROR;
	}
	lib_log(slot->ctx, LED_LOG_LEVEL_DEBUG,
		"SUCCESS write buffer %s after: 0x%x\n", slot->address,
		(unsigned int)get_int(slot->sysfs_path, 0, "attention"));

	return STATUS_SUCCESS;
}

status_t vmdssd_write(struct block_device *device, enum led_ibpi_pattern ibpi)
{
	struct pci_slot *slot;
	char *short_name = strrchr(device->sysfs_path, '/');
	int ret;

	if (short_name)
		short_name++;
	else
		short_name = device->sysfs_path;

	if (ibpi == device->ibpi_prev)
		return STATUS_SUCCESS;

	if ((ibpi < LED_IBPI_PATTERN_NORMAL) || (ibpi > LED_IBPI_PATTERN_LOCATE_OFF))
		return STATUS_INVALID_STATE;

	slot = vmdssd_find_pci_slot(device->cntrl->ctx, device->sysfs_path);
	if (!slot) {
		lib_log(device->cntrl->ctx, LED_LOG_LEVEL_DEBUG,
			"PCI hotplug slot not found for %s\n", short_name);
		return STATUS_NULL_POINTER;
	}

	if (access(ACPI_DSM_DEVICE, F_OK) == 0)
		ret = vmdssd_acpi_led_control(device, ibpi);
	else
		ret = vmdssd_write_attention_buf(slot, ibpi);

	if (ret == STATUS_SUCCESS)
		device->ibpi_prev = ibpi;
	return ret;
}

char *vmdssd_get_path(const char *cntrl_path)
{
	return strdup(cntrl_path);
}

static bool vmdssd_get_rootport_bdf_from_syspath(const char *sysfs_path,
                         uint32_t *domain, uint8_t *bus,
                         uint8_t *dev, uint8_t *fn)
{
    char tmp[PATH_MAX];
    char prev_bdf[32] = {0};
    char *cur, *next;
    unsigned int dmn, b, d, f;

    if (!sysfs_path || !domain || !bus || !dev || !fn)
        return false;

    if (strlen(sysfs_path) >= PATH_MAX)
        return false;

    strcpy(tmp, sysfs_path);
    cur = strtok(tmp, "/");
    while (cur) {
        next = strtok(NULL, "/");

        /* when next is "nvme": cur=endpoint, prev_bdf=rootport */
        if (next && strcmp(next, "nvme") == 0) {
            const char *rootport = prev_bdf[0] ? prev_bdf : cur;

            if (sscanf(rootport, "%x:%x:%x.%x", &dmn, &b, &d, &f) != 4)
                return false;

            *domain = (uint32_t)dmn;
            *bus    = (uint8_t)b;
            *dev    = (uint8_t)d;
            *fn     = (uint8_t)f;
            return true;
        }

        /* track the last full PCI BDF token seen (domain:bus:dev.fn) */
        if (strchr(cur, ':') && strchr(cur, '.'))
            strncpy(prev_bdf, cur, sizeof(prev_bdf) - 1);

        cur = next;
    }

    return false;
}

static status_t vmdssd_acpi_led_control(struct block_device *device, enum led_ibpi_pattern ibpi)
{
    uint64_t led_function, led_arg;
	uint8_t led_state = 0, bus = 0, dev = 0, fn = 0;
	struct pci_slot *slot;
	uint32_t domain;
    int ret;

    if (!device || !device->cntrl || device->sysfs_path[0] == '\0') {
        lib_log(NULL, LED_LOG_LEVEL_ERROR, "Invalid device parameter");
        return STATUS_INVALID_PATH;
    }

    slot = vmdssd_find_pci_slot(device->cntrl->ctx, device->sysfs_path);
    if (!slot)
        return STATUS_NULL_POINTER;

    if (!vmdssd_get_rootport_bdf_from_syspath(device->sysfs_path,
                                              &domain, &bus, &dev, &fn)) {
        lib_log(device->cntrl->ctx, LED_LOG_LEVEL_ERROR,
                "Failed to parse rootport BDF from %s", device->sysfs_path);
        return STATUS_INVALID_PATH;
    }

    lib_log(device->cntrl->ctx, LED_LOG_LEVEL_ERROR,
            "rootport BDF = %05x:%02x:%02x.%u", domain, bus, dev, fn);

    // Map IBPI pattern to DSM function and argument
    switch (ibpi) {
    case LED_IBPI_PATTERN_NORMAL:
    case LED_IBPI_PATTERN_LOCATE_OFF:
        led_function = 0x01;
        led_state = 0x00;
        break;
	case LED_IBPI_PATTERN_LOCATE:
        led_function = 0x01;
		led_state = 0x01;
        break;
    case LED_IBPI_PATTERN_FAILED_DRIVE:
        led_function = 0x01;
        led_state = 0x02;
        break;
    case LED_IBPI_PATTERN_REBUILD:
        led_function = 0x01;
        led_state = 0x03;
        break;
    default:
        lib_log(device->cntrl->ctx, LED_LOG_LEVEL_ERROR, "Unsupported IBPI pattern:");
        return STATUS_INVALID_STATE;
    }

    led_arg = ((uint64_t)bus << 24) |
              ((uint64_t)dev << 16) |
              ((uint64_t)fn  << 8)  |
              (uint64_t)led_state;

    lib_log(device->cntrl->ctx, LED_LOG_LEVEL_ERROR,
            "Calling ACPI DSM: func=%lu, arg=%lu, led state=%u",
            led_function, led_arg, (unsigned int)led_state);

    ret = vmdssd_acpi_dsm_call(VMD_LED_DSM_UUID, led_function, led_arg);
    
    if (ret == STATUS_SUCCESS) {
        lib_log(device->cntrl->ctx, LED_LOG_LEVEL_ERROR,
                "VMD LED control via ACPI DSM successful for %s", device->sysfs_path);
        return STATUS_SUCCESS;
    }
	return ret;
}

static status_t vmdssd_acpi_dsm_call(const char *guid_str,
			       uint64_t function, uint64_t arg)
{
	static uuid_t cached_guid;
	static bool guid_parsed = false;
	struct vmd_dsm_request req = {0};
	int fd;

	if (!guid_parsed) {
		if (uuid_parse(guid_str, cached_guid) != 0) {
			lib_log(NULL, LED_LOG_LEVEL_ERROR,
				"Failed to parse UUID: %s", guid_str);
			return STATUS_INVALID_PATH;
		}
		guid_parsed = true;
	}

	fd = open(ACPI_DSM_DEVICE, O_RDWR);
	if (fd < 0) {
		lib_log(NULL, LED_LOG_LEVEL_DEBUG,
			"Failed to open ACPI DSM device: %s", strerror(errno));
		return STATUS_INVALID_NODE;
	}

	/* Kernel resolves ACPI scope dynamically (vmd_acpi_find_companion). */
	memcpy(req.guid, cached_guid, sizeof(cached_guid));
	req.rev = 1;
	req.func = function;
	req.arg = arg;

	if (ioctl(fd, VMD_DSM_IOCTL, &req) < 0) {
		lib_log(NULL, LED_LOG_LEVEL_DEBUG,
			"ACPI DSM call failed: %s", strerror(errno));
		close(fd);
		return STATUS_FILE_WRITE_ERROR;
	}

	close(fd);
	return STATUS_SUCCESS;
}
