// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2023, Advanced Micro Devices, Inc.

/* AMD SGPIO LED control */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/file.h>
#include "config.h"

#if _HAVE_DMALLOC_H
#include <dmalloc.h>
#endif

#include "led/libled.h"
#include "utils.h"
#include "amd.h"
#include "amd_sgpio.h"
#include "block.h"
#include "cntrl.h"
#include "list.h"
#include "libled_private.h"
#include "slot.h"

#define HOST_CAP_EMS	(1 << 6)

#define DECLARE_SGPIO(type, name, shift, mask)				\
	uint32_t	_##type##_##name##_shift = shift;		\
	uint64_t	_##type##_##name##_mask = mask << shift;	\
									\
	static void set_sgpio_##type##_##name(sgpio_##type##_t * hdr,	\
					      sgpio_##type##_t val)	\
	{								\
		*hdr |= (val << _##type##_##name##_shift);		\
	}								\
									\
	static uint32_t get_sgpio_##type##_##name(sgpio_##type##_t * hdr)\
	{								\
		return (*hdr & _##type##_##name##_mask)			\
			>> _##type##_##name##_shift;			\
	}


#define DECLARE_SGPIO_RO(type, name, shift, mask)			\
	uint32_t	_##type##_##name##_shift = shift;		\
	uint64_t	_##type##_##name##_mask = mask;			\
									\
	static uint32_t get_sgpio_##type##_##name(sgpio_##type##_t * hdr)\
	{								\
		return (*hdr & _##type##_##name##_mask)			\
			>> _##type##_##name##_shift;			\
	}


typedef uint32_t sgpio_hdr_t;

#define SGPIO_HDR_MSG_TYPE_SGPIO	0x03

DECLARE_SGPIO(hdr, msg_type, 4, 0xF)
DECLARE_SGPIO(hdr, data_size, 8, 0xFF)
DECLARE_SGPIO(hdr, msg_size, 16, 0xFF)

typedef uint64_t sgpio_req_t;

#define SGPIO_REQ_REG_TYPE_CFG		0x00
#define SGPIO_REQ_REG_TYPE_TX		0x03
#define SGPIO_REQ_REG_TYPE_AMD		0xC0

DECLARE_SGPIO(req, frame_type, 0, 0xFFL)
DECLARE_SGPIO(req, function, 8, 0xFFL)
DECLARE_SGPIO(req, reg_type, 16, 0xFFL)
DECLARE_SGPIO(req, reg_index, 24, 0xFFL)
DECLARE_SGPIO(req, reg_count, 32, 0xFFULL)

typedef uint32_t sgpio_amd_t;

DECLARE_SGPIO(amd, initiator, 0, 0x1)
DECLARE_SGPIO(amd, polarity_flip, 4, 0x1)
DECLARE_SGPIO(amd, return_to_normal, 5, 0x1)
DECLARE_SGPIO(amd, bypass_enable, 6, 0x1)

typedef uint64_t sgpio_cfg_t;

DECLARE_SGPIO_RO(cfg, version, 8, 0xF);
DECLARE_SGPIO_RO(cfg, gp_reg_count, 16, 0xF);
DECLARE_SGPIO_RO(cfg, cfg_reg_count, 20, 0x7);
DECLARE_SGPIO(cfg, gpio_enable, 23, 0x1);
DECLARE_SGPIO_RO(cfg, drive_count, 24, 0xFF);

DECLARE_SGPIO(cfg, blink_gen_a, 40, 0xFULL);
DECLARE_SGPIO(cfg, blink_gen_b, 44, 0xFULL);
DECLARE_SGPIO(cfg, max_on, 48, 0xFULL);
DECLARE_SGPIO(cfg, force_off, 52, 0xFULL);
DECLARE_SGPIO(cfg, stretch_on, 56, 0xFULL);
DECLARE_SGPIO(cfg, stretch_off, 60, 0xFULL);

#define DECLARE_LED(name, shift, mask)					\
	uint32_t	_led_##name##_shift = shift;			\
	uint64_t	_led_##name##_mask = mask;			\
									\
	static void set_##name##_led(drive_led_t *hdr, uint32_t val)	\
	{								\
		*hdr |= (val << _led_##name##_shift);			\
	}								\
									\
	static uint32_t get_##name##_led(drive_led_t *hdr)		\
	{								\
		return (*hdr & _led_##name##_mask) >> _led_##name##_shift;\
	}

DECLARE_LED(error, 0, 0x07);
DECLARE_LED(locate, 3, 0x18);
DECLARE_LED(activity, 5, 0xE0);

typedef struct sgpio_transmit_register {
	drive_led_t	drive[4];
} __attribute__ ((__packed__)) sgpio_tx_t;

struct amd_register {
	sgpio_hdr_t	hdr;
	sgpio_req_t	req;
	sgpio_amd_t	amd;
} __attribute__ ((__packed__));

struct config_register {
	sgpio_hdr_t	hdr;
	sgpio_req_t	req;
	sgpio_cfg_t	cfg;
} __attribute__ ((__packed__));

struct transmit_register {
	sgpio_hdr_t	hdr;
	sgpio_req_t	req;
	sgpio_tx_t	tx;
} __attribute__ ((__packed__));

const uint8_t ibpi_pattern[] = {
	[LED_IBPI_PATTERN_NONE]		= 0x00,
	[LED_IBPI_PATTERN_REBUILD]		= 0x07,
	[LED_IBPI_PATTERN_HOTSPARE]		= 0x02,
	[LED_IBPI_PATTERN_PFA]		= 0x03,
	[LED_IBPI_PATTERN_FAILED_DRIVE]	= 0x00,
	[LED_IBPI_PATTERN_LOCATE]		= 0x07,
	[LED_IBPI_PATTERN_LOCATE_OFF]	= 0x00
};

#define INIT_LED(e, l, a)	{.error = e, .locate = l, .activity = a}
const struct drive_leds tx_leds_blink_gen_a[] = {
	[LED_IBPI_PATTERN_NORMAL] =		INIT_LED(0, 0, 0b101),
	[LED_IBPI_PATTERN_ONESHOT_NORMAL] =	INIT_LED(0, 0, 0b101),
	[LED_IBPI_PATTERN_REBUILD] =	INIT_LED(0b010, 0, 0),
	[LED_IBPI_PATTERN_HOTSPARE]	=	INIT_LED(0b010, 0, 0),
	[LED_IBPI_PATTERN_PFA] =		INIT_LED(0b010, 0, 0),
	[LED_IBPI_PATTERN_FAILED_DRIVE] =	INIT_LED(0b001, 0, 0),
	[LED_IBPI_PATTERN_LOCATE] =		INIT_LED(0b010, 0, 0b010),
	[LED_IBPI_PATTERN_LOCATE_OFF] =	INIT_LED(0, 0, 0b101),
	[99] = INIT_LED(0, 0, 0b101),
};

const struct drive_leds tx_leds_blink_gen_b[] = {
	[LED_IBPI_PATTERN_NORMAL] =		INIT_LED(0, 0, 0b101),
	[LED_IBPI_PATTERN_ONESHOT_NORMAL] =	INIT_LED(0, 0, 0b101),
	[LED_IBPI_PATTERN_REBUILD] =	INIT_LED(0b110, 0, 0),
	[LED_IBPI_PATTERN_HOTSPARE]	=	INIT_LED(0b110, 0, 0),
	[LED_IBPI_PATTERN_PFA] =		INIT_LED(0b110, 0, 0),
	[LED_IBPI_PATTERN_FAILED_DRIVE] =	INIT_LED(0b001, 0, 0),
	[LED_IBPI_PATTERN_LOCATE] =		INIT_LED(0b110, 0, 0b110),
	[LED_IBPI_PATTERN_LOCATE_OFF] =	INIT_LED(0, 0, 0b101),
	[99] = INIT_LED(0, 0, 0b101),
};

#define CACHE_SZ	1024

static void _put_cache(struct led_ctx *ctx)
{
	munmap(ctx->amd_sgpio.cache, CACHE_SZ);

	if (ctx->amd_sgpio.cache_fd) {
		flock(ctx->amd_sgpio.cache_fd, LOCK_UN);
		fsync(ctx->amd_sgpio.cache_fd);
		close(ctx->amd_sgpio.cache_fd);
		ctx->amd_sgpio.cache_fd = 0;
	}
}

void amd_sgpio_cache_free(struct led_ctx *ctx)
{
	if (ctx->amd_sgpio.cache) {
		_put_cache(ctx);
	}
}

static int _open_and_map_cache(struct led_ctx *ctx)
{
	struct stat sbuf;
	int rc = 0;

	if (ctx->amd_sgpio.cache_fd)
		return 0;

	ctx->amd_sgpio.cache_fd = shm_open("/ledmon_amd_sgpio_cache", O_RDWR | O_CREAT,
			    S_IRUSR | S_IWUSR);
	if (ctx->amd_sgpio.cache_fd < 1) {
		lib_log(ctx, LED_LOG_LEVEL_ERROR,
			"Couldn't open SGPIO cache: %s", strerror(errno));
		return -1;
	}

	rc = flock(ctx->amd_sgpio.cache_fd, LOCK_EX);
	if (rc != 0) {
		lib_log(ctx, LED_LOG_LEVEL_ERROR,
			"Couldn't lock SGPIO cache: %s", strerror(errno));
		return -1;
	}

	rc = fstat(ctx->amd_sgpio.cache_fd, &sbuf);
	if (rc != 0) {
		lib_log(ctx, LED_LOG_LEVEL_ERROR,
			"Couldn't stat SGPIO cache: %s", strerror(errno));
		return -1;
	}

	if (sbuf.st_size == 0) {
		if (ftruncate(ctx->amd_sgpio.cache_fd, CACHE_SZ) != 0) {
			lib_log(ctx, LED_LOG_LEVEL_ERROR,
				"Couldn't truncate SGPIO cache: %s", strerror(errno));
			return -1;
		}
	}

	ctx->amd_sgpio.cache = mmap(NULL, CACHE_SZ, PROT_READ | PROT_WRITE,
			   MAP_SHARED, ctx->amd_sgpio.cache_fd, 0);
	if (ctx->amd_sgpio.cache == MAP_FAILED) {
		lib_log(ctx, LED_LOG_LEVEL_ERROR,
			"Couldn't map SGPIO cache: %s", strerror(errno));
		_put_cache(ctx);
		return -1;
	}

	return 0;
}

static struct cache_entry *_get_cache(struct amd_drive *drive)
{
	int rc, index;

	rc = _open_and_map_cache(drive->ctx);
	if (rc)
		return NULL;

	/* The sgpio_cache is just an array of cache_entry structs, and
	 * each cache_entry describes the drive LED settings for 4 drives.
	 * To find the corresponding cache entry for an ata port we need
	 * to first round down to the nearest multiple of 4, then divide
	 * by four. This gives us the following mapping;
	 *	cache_entry[0] => drive 0 to drive 3
	 *	cache_entry[1] => drive 4 to drive 7
	 *	cache_entry[n] => drive (4*n) to drive (4*n + 3)
	 */

	index = ((drive->ata_port - 1) / 4);

	return &drive->ctx->amd_sgpio.cache[index];
}

static int _send_sgpio_register(struct led_ctx *ctx, const char *em_buffer_path, void *reg,
				int reg_len)
{
	int count;
	int saved_errno;
	int retries = 3;

	do {
		int fd = open(em_buffer_path, O_WRONLY);

		if (fd < 0) {
			lib_log(ctx, LED_LOG_LEVEL_ERROR, "Couldn't open EM buffer %s: %s",
				em_buffer_path, strerror(errno));
			return -1;
		}

		count = write(fd, reg, reg_len);
		saved_errno = errno;
		close(fd);

		/* Insert small sleep to ensure hardware has enough time to
		 * see the register change and read it. Without the sleep
		 * multiple writes can result in an EBUSY return because
		 * hardware has not cleared the EM_CTL_TM (Transmit Message)
		 * bit.
		 */
		usleep(1000);

		if (count == reg_len || saved_errno != EBUSY)
			break;
	} while (--retries != 0);

	if (count != reg_len) {
		lib_log(ctx, LED_LOG_LEVEL_ERROR, "Couldn't write SGPIO register: %s",
			strerror(saved_errno));
		return -1;
	}

	return 0;
}

static sgpio_hdr_t _init_sgpio_hdr(int data_size, int msg_size)
{
	sgpio_hdr_t hdr = 0;

	set_sgpio_hdr_msg_type(&hdr, SGPIO_HDR_MSG_TYPE_SGPIO);
	set_sgpio_hdr_data_size(&hdr, data_size);
	set_sgpio_hdr_msg_size(&hdr, msg_size);

	return hdr;
}

static void _dump_sgpio_hdr(struct led_ctx *ctx, const char *type, sgpio_hdr_t hdr)
{
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, "%s SGPIO Header: %08x\n", type, hdr);
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, REG_FMT_2, "message type",
		get_sgpio_hdr_msg_type(&hdr),
		"data size", get_sgpio_hdr_data_size(&hdr));
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, REG_FMT_1, "message size",
		get_sgpio_hdr_msg_size(&hdr));
}

static sgpio_req_t _init_sgpio_req(int frame_type, int function,
				   int reg_type, int reg_index,
				   int reg_count)
{
	sgpio_req_t req = 0;

	set_sgpio_req_frame_type(&req, frame_type);
	set_sgpio_req_function(&req, function);
	set_sgpio_req_reg_type(&req, reg_type);
	set_sgpio_req_reg_index(&req, reg_index);
	set_sgpio_req_reg_count(&req, reg_count);

	return req;
}

static void _dump_sgpio_req(struct led_ctx *ctx, const char *type, sgpio_req_t req)
{
	uint32_t *r = (uint32_t *)&req;

	lib_log(ctx, LED_LOG_LEVEL_DEBUG, "%s SGPIO Request Register: %08x %08x\n",
		type, r[0], r[1]);
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, REG_FMT_2, "frame type",
		get_sgpio_req_frame_type(&req),
		  "function", get_sgpio_req_function(&req));
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, REG_FMT_2, "register type",
		get_sgpio_req_reg_type(&req),
		  "register index", get_sgpio_req_reg_index(&req));
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, REG_FMT_1, "register count",
		get_sgpio_req_reg_count(&req));
}

static sgpio_cfg_t _init_sgpio_cfg(int gpio_enable, int blink_a,
				   int blink_b, int force_off, int max_on,
				   int stretch_off, int stretch_on)
{
	sgpio_cfg_t cfg = 0;

	if (gpio_enable)
		set_sgpio_cfg_gpio_enable(&cfg, 1);

	set_sgpio_cfg_blink_gen_a(&cfg, blink_a);
	set_sgpio_cfg_blink_gen_b(&cfg, blink_b);
	set_sgpio_cfg_max_on(&cfg, max_on);
	set_sgpio_cfg_force_off(&cfg, force_off);
	set_sgpio_cfg_stretch_on(&cfg, stretch_on);
	set_sgpio_cfg_stretch_off(&cfg, stretch_off);

	return cfg;
}

static void _dump_sgpio_cfg(struct led_ctx *ctx, const char *type, sgpio_cfg_t cfg)
{
	uint32_t *r = (uint32_t *)&cfg;

	lib_log(ctx, LED_LOG_LEVEL_DEBUG, "%s SGPIO Configuration Register: %08x %08x\n", type,
		  r[0], r[1]);
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, REG_FMT_2, "version", get_sgpio_cfg_version(&cfg),
		  "gp register count", get_sgpio_cfg_gp_reg_count(&cfg));
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, REG_FMT_2, "cfg register count",
		  get_sgpio_cfg_cfg_reg_count(&cfg), "gpio enabled",
		  get_sgpio_cfg_gpio_enable(&cfg));
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, REG_FMT_2, "drive count",
		get_sgpio_cfg_drive_count(&cfg),
		"blink gen rate A", get_sgpio_cfg_blink_gen_a(&cfg));

	lib_log(ctx, LED_LOG_LEVEL_DEBUG, REG_FMT_2, "blink gen rate B",
		  get_sgpio_cfg_blink_gen_b(&cfg), "force activity off",
		  get_sgpio_cfg_force_off(&cfg));
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, REG_FMT_2, "max activity on",
		get_sgpio_cfg_max_on(&cfg), "stretch activity off",
		get_sgpio_cfg_stretch_off(&cfg));
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, REG_FMT_1, "stretch activity on",
		  get_sgpio_cfg_stretch_on(&cfg));
}

static sgpio_amd_t _init_sgpio_amd(int initiator, int polarity,
				   int bypass, int normal)
{
	sgpio_amd_t amd = 0;

	set_sgpio_amd_initiator(&amd, initiator);
	set_sgpio_amd_polarity_flip(&amd, polarity);
	set_sgpio_amd_bypass_enable(&amd, bypass);
	set_sgpio_amd_return_to_normal(&amd, normal);

	return amd;
}

static void _dump_sgpio_amd(struct led_ctx *ctx, const char *type, sgpio_amd_t amd)
{
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, "%s SGPIO AMD Register: %08x\n", type, amd);
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, REG_FMT_2, "initiator", get_sgpio_amd_initiator(&amd),
		  "polarity", get_sgpio_amd_polarity_flip(&amd));
	lib_log(ctx, LED_LOG_LEVEL_DEBUG, REG_FMT_2, "bypass enable",
		get_sgpio_amd_bypass_enable(&amd), "return to normal",
		get_sgpio_amd_return_to_normal(&amd));
}

static int _write_cfg_register(struct led_ctx *ctx, const char *em_buffer_path,
			       struct cache_entry *cache, int ibpi)
{
	struct config_register cfg_reg;

	cfg_reg.hdr = _init_sgpio_hdr(0, sizeof(cfg_reg));
	cfg_reg.req = _init_sgpio_req(0x40, 0x82, SGPIO_REQ_REG_TYPE_CFG,
				      0, 2);

	if (cache->blink_gen_a)
		cache->blink_gen_b = ibpi_pattern[ibpi];
	else
		cache->blink_gen_a = ibpi_pattern[ibpi];

	cfg_reg.cfg = _init_sgpio_cfg(1, cache->blink_gen_a,
				      cache->blink_gen_b, 2, 1, 0, 0);

	_dump_sgpio_hdr(ctx, "CFG", cfg_reg.hdr);
	_dump_sgpio_req(ctx, "CFG", cfg_reg.req);
	_dump_sgpio_cfg(ctx, "CFG", cfg_reg.cfg);

	return _send_sgpio_register(ctx, em_buffer_path, &cfg_reg, sizeof(cfg_reg));
}

static void _dump_sgpio_tx(struct led_ctx *ctx, const char *type, sgpio_tx_t tx)
{
	int i;

	lib_log(ctx, LED_LOG_LEVEL_DEBUG, "%s SGPIO TX Register: %02x:%02x:%02x:%02x\n",
		type, tx.drive[0], tx.drive[1], tx.drive[2], tx.drive[3]);
	for (i = 0; i < 4; i++) {
		lib_log(ctx, LED_LOG_LEVEL_DEBUG,
			"\tdrive %d: error %x, locate %x, activity %x\n", i,
			get_error_led(&tx.drive[i]),
			get_locate_led(&tx.drive[i]),
			get_activity_led(&tx.drive[i]));
	}
}

static int _write_tx_register(struct led_ctx *ctx, const char *em_buffer_path,
			      struct transmit_register *tx_reg)
{
	tx_reg->hdr = _init_sgpio_hdr(0, sizeof(*tx_reg));
	tx_reg->req = _init_sgpio_req(0x40, 0x82, SGPIO_REQ_REG_TYPE_TX,
				      0, 1);

	_dump_sgpio_hdr(ctx, "TX", tx_reg->hdr);
	_dump_sgpio_req(ctx, "TX", tx_reg->req);
	_dump_sgpio_tx(ctx, "TX", tx_reg->tx);

	return _send_sgpio_register(ctx, em_buffer_path, tx_reg, sizeof(*tx_reg));
}

static void _set_tx_drive_leds(struct transmit_register *tx_reg,
			       struct cache_entry *cache,
			       int drive_bay, int ibpi)
{
	int i;
	const struct drive_leds *leds;

	memset(&tx_reg->tx, 0, sizeof(tx_reg->tx));

	if (cache->blink_gen_a)
		leds = &tx_leds_blink_gen_b[ibpi];
	else
		leds = &tx_leds_blink_gen_a[ibpi];

	cache->leds[drive_bay].error = leds->error;
	cache->leds[drive_bay].locate = leds->locate;
	cache->leds[drive_bay].activity = leds->activity;

	for (i = 0; i < 4; i++) {
		set_error_led(&tx_reg->tx.drive[i], cache->leds[i].error);
		set_locate_led(&tx_reg->tx.drive[i], cache->leds[i].locate);
		set_activity_led(&tx_reg->tx.drive[i], cache->leds[i].activity);
	}
}

static int _init_tx_drive_leds(struct transmit_register *tx_reg,
			       struct cache_entry *cache)
{
	int i;
	int init_done = 0;

	memset(tx_reg, 0, sizeof(*tx_reg));

	for (i = 0; i < 4; i++) {
		if (cache->leds[i].error || cache->leds[i].locate ||
		    cache->leds[i].activity)
			continue;

		_set_tx_drive_leds(tx_reg, cache, i, 99);
		init_done = 1;
	}

	return init_done;
}

static int _write_amd_register(const char *em_buffer_path,
			       struct amd_drive *drive)
{
	struct amd_register amd_reg;

	amd_reg.hdr = _init_sgpio_hdr(0, sizeof(amd_reg));
	amd_reg.req = _init_sgpio_req(0x40, 0x82, SGPIO_REQ_REG_TYPE_AMD,
				      0, 1);
	amd_reg.amd = _init_sgpio_amd(drive->initiator, 0, 1, 1);

	_dump_sgpio_hdr(drive->ctx, "AMD", amd_reg.hdr);
	_dump_sgpio_req(drive->ctx, "AMD", amd_reg.req);
	_dump_sgpio_amd(drive->ctx, "AMD", amd_reg.amd);

	return _send_sgpio_register(drive->ctx, em_buffer_path, &amd_reg, sizeof(amd_reg));
}

static int _get_amd_sgpio_drive(const char *start_path,
				struct amd_drive *drive)
{
	char *a, *p;
	int found;
	char path[PATH_MAX];
	char ata_dir[PATH_MAX];

	/* Start the search at the ataXX directory */
	strncpy(ata_dir, start_path, PATH_MAX);
	ata_dir[PATH_MAX - 1] = 0;
	a = p = strstr(ata_dir, "ata");
	if (!p) {
		lib_log(drive->ctx, LED_LOG_LEVEL_INFO, "Couldn't find ata path for %s",
			start_path);
		return -1;
	}

	/* terminate the path after the ataXX/ part */
	p = strchr(p, '/');
	if (!p)
		return 1;
	*p = '\0';

	/* skip past 'ata' to get the ata port number */
	a += 3;
	if (str_toi(&drive->ata_port, a, NULL, 10) != 0)
		return -1;

	found = _find_file_path(ata_dir, "port_no", path, PATH_MAX, drive->ctx);
	if (!found) {
		lib_log(drive->ctx, LED_LOG_LEVEL_INFO,
			"Couldn't find 'port_no' for %s\n", ata_dir);
		return -1;
	}

	drive->port = get_int(path, -1, "port_no");

	if (drive->port == -1)
		return -1;

	drive->drive_bay = 8 - drive->port;
	if (drive->drive_bay < 4) {
		drive->initiator = 1;
	} else {
		drive->drive_bay -= 4;
		drive->initiator = 0;
	}

	lib_log(drive->ctx, LED_LOG_LEVEL_DEBUG,
		"AMD Drive: port %d, ata port %d, drive bay %d, initiator %d",
		drive->port, drive->ata_port, drive->drive_bay,
		drive->initiator);
	return 0;
}

static int _amd_sgpio_set_drive(struct led_ctx *ctx, const char *em_buffer_path,
				struct amd_drive *drive, enum led_ibpi_pattern ibpi);

static int _set_ibpi(struct block_device *device, enum led_ibpi_pattern ibpi)
{
	int rc;
	struct amd_drive drive;

	memset(&drive, 0, sizeof(struct amd_drive));
	drive.ctx = device->cntrl->ctx;

	lib_log(drive.ctx, LED_LOG_LEVEL_INFO, "\n");
	lib_log(drive.ctx, LED_LOG_LEVEL_INFO, "Setting %s...", ibpi2str(ibpi));
	lib_log(drive.ctx, LED_LOG_LEVEL_DEBUG, "\tdevice: ...%s",
		strstr(device->sysfs_path, "/ata"));
	lib_log(drive.ctx, LED_LOG_LEVEL_DEBUG, "\tbuffer: ...%s",
		strstr(device->cntrl_path, "/ata"));

	/* Retrieve the port number and correlate that to the drive slot.
	 * Port numbers 8..1 correspond to slot numbers 0..7. This is
	 * further broken down (since we can only control 4 drive slots)
	 * such that initiator = 1 for slots 0..3 and initiator = 0 for
	 * slots 4..7, the slot value is reduced by 4 for slots 4..7 so
	 * we can calculate the correct bits to set in the register for
	 * that drive.
	 */
	rc = _get_amd_sgpio_drive(device->sysfs_path, &drive);
	if (rc)
		return rc;

	return _amd_sgpio_set_drive(device->cntrl->ctx, device->cntrl_path, &drive, ibpi);
}

static int _amd_sgpio_set_drive(struct led_ctx *ctx, const char *em_buffer_path,
				struct amd_drive *drive, enum led_ibpi_pattern ibpi)
{
	int rc;
	struct transmit_register tx_reg;
	struct cache_entry *cache;
	struct cache_entry cache_dup;

	cache = _get_cache(drive);
	if (!cache)
		return -EINVAL;

	memcpy(&cache_dup, cache, sizeof(cache_dup));

	rc = _write_amd_register(em_buffer_path, drive);
	if (rc)
		goto error;

	rc = _write_cfg_register(ctx, em_buffer_path, cache, ibpi);
	if (rc)
		goto error;

	memset(&tx_reg, 0, sizeof(tx_reg));
	_set_tx_drive_leds(&tx_reg, cache, drive->drive_bay, ibpi);
	rc = _write_tx_register(ctx, em_buffer_path, &tx_reg);

error:
	if (rc)
		memcpy(cache, &cache_dup, sizeof(*cache));

	_put_cache(ctx);
	return rc;
}

static status_t amd_sgpio_set_slot(struct slot_property *slot, enum led_ibpi_pattern ibpi)
{
	struct amd_sgpio_slot *amd_slot = &slot->slot_spec.amd_sgpio;
	struct amd_drive drive = {
		.ata_port = amd_slot->ata_port,
		.port = amd_slot->port,
		.drive_bay = amd_slot->drive_bay,
		.initiator = amd_slot->initiator,
		.ctx = amd_slot->ctx,
	};

	if ((ibpi < LED_IBPI_PATTERN_NORMAL) || (ibpi > LED_IBPI_PATTERN_LOCATE_OFF))
		return STATUS_INVALID_STATE;

	if ((ibpi == LED_IBPI_PATTERN_DEGRADED) ||
	    (ibpi == LED_IBPI_PATTERN_FAILED_ARRAY))
		return STATUS_INVALID_STATE;

	if (_amd_sgpio_set_drive(amd_slot->ctx, amd_slot->em_buffer_path, &drive, ibpi))
		return STATUS_FILE_WRITE_ERROR;
	return STATUS_SUCCESS;
}

static bool drive_leds_equal(const struct drive_leds *a, const struct drive_leds *b)
{
	return a->error == b->error && a->locate == b->locate && a->activity == b->activity;
}

static enum led_ibpi_pattern amd_sgpio_get_slot_state(struct slot_property *slot)
{
	struct amd_sgpio_slot *amd_slot = &slot->slot_spec.amd_sgpio;
	struct amd_drive drive = {
		.ata_port = amd_slot->ata_port,
		.port = amd_slot->port,
		.drive_bay = amd_slot->drive_bay,
		.initiator = amd_slot->initiator,
		.ctx = amd_slot->ctx,
	};
	struct cache_entry *cache;
	struct drive_leds *leds;
	enum led_ibpi_pattern ibpi;

	cache = _get_cache(&drive);
	if (!cache)
		return LED_IBPI_PATTERN_UNKNOWN;

	leds = &cache->leds[amd_slot->drive_bay];

	for (ibpi = LED_IBPI_PATTERN_NORMAL; ibpi <= LED_IBPI_PATTERN_LOCATE_OFF; ibpi++) {
		if (ibpi == LED_IBPI_PATTERN_DEGRADED ||
		    ibpi == LED_IBPI_PATTERN_FAILED_ARRAY)
			continue;

		if (drive_leds_equal(leds, &tx_leds_blink_gen_a[ibpi]) ||
		    drive_leds_equal(leds, &tx_leds_blink_gen_b[ibpi])) {
			_put_cache(amd_slot->ctx);
			return ibpi == LED_IBPI_PATTERN_LOCATE_OFF ?
			       LED_IBPI_PATTERN_NORMAL : ibpi;
		}
	}

	_put_cache(amd_slot->ctx);
	return LED_IBPI_PATTERN_UNKNOWN;
}

static const struct slot_property_common amd_sgpio_slot_common = {
	.cntrl_type = LED_CNTRL_TYPE_AMD,
	.get_state_fn = amd_sgpio_get_slot_state,
	.set_slot_fn = amd_sgpio_set_slot,
};

static struct slot_property *amd_sgpio_slot_property_init(struct cntrl_device *cntrl,
							  const char *em_buffer_path)
{
	struct slot_property *slot;
	struct amd_sgpio_slot *amd_slot;
	struct amd_drive drive;
	char ata_name[32];

	slot = calloc(1, sizeof(*slot));
	if (!slot)
		return NULL;

	amd_slot = &slot->slot_spec.amd_sgpio;
	memset(&drive, 0, sizeof(drive));
	drive.ctx = cntrl->ctx;
	if (_get_amd_sgpio_drive(em_buffer_path, &drive))
		goto error;

	str_cpy(amd_slot->em_buffer_path, em_buffer_path, PATH_MAX);
	amd_slot->ata_port = drive.ata_port;
	amd_slot->port = drive.port;
	amd_slot->drive_bay = drive.drive_bay;
	amd_slot->initiator = drive.initiator;
	amd_slot->ctx = cntrl->ctx;

	snprintf(ata_name, sizeof(ata_name), "ata%d", amd_slot->ata_port);
	slot->bl_device = get_block_device_from_sysfs_path(cntrl->ctx, ata_name, false);
	slot->c = &amd_sgpio_slot_common;
	snprintf(slot->slot_id, PATH_MAX, "ata%d-bay%d",
		 amd_slot->ata_port, amd_slot->drive_bay);

	return slot;

error:
	free(slot);
	return NULL;
}

static void amd_sgpio_em_buffer_add(struct cntrl_device *cntrl, struct list *slots,
				    const char *em_buffer_path)
{
	struct slot_property *slot = amd_sgpio_slot_property_init(cntrl, em_buffer_path);

	if (slot)
		list_append_ctx(slots, slot, cntrl->ctx);
}

static void amd_sgpio_scan_slots(struct cntrl_device *cntrl, struct list *slots,
				 const char *path)
{
	struct list dir;
	const char *dir_path;

	if (scan_dir(path, &dir))
		return;

	list_for_each(&dir, dir_path) {
		struct stat sbuf;
		const char *name = strrchr(dir_path, '/');

		if (!name)
			continue;
		name++;

		if (strcmp(name, "em_buffer") == 0) {
			amd_sgpio_em_buffer_add(cntrl, slots, dir_path);
			continue;
		}

		if (lstat(dir_path, &sbuf) == -1)
			continue;

		if (S_ISDIR(sbuf.st_mode))
			amd_sgpio_scan_slots(cntrl, slots, dir_path);
	}

	list_erase(&dir);
}

void amd_sgpio_slots_add(struct cntrl_device *cntrl, struct list *slots)
{
	if (amd_interface != AMD_INTF_SGPIO)
		return;

	amd_sgpio_scan_slots(cntrl, slots, cntrl->sysfs_path);
}

static int _amd_sgpio_init_one(const char *path, struct amd_drive *drive,
			       struct cache_entry *cache)
{
	int rc, do_init;
	struct transmit_register tx_reg;

	do_init = _init_tx_drive_leds(&tx_reg, cache);
	if (!do_init)
		return 0;

	lib_log(drive->ctx, LED_LOG_LEVEL_DEBUG,
		"Initializing host %d..%d:", drive->ata_port, drive->ata_port + 3);
	lib_log(drive->ctx, LED_LOG_LEVEL_DEBUG, "\tbuffer: %s", strstr(path, "/ata"));

	rc = _write_amd_register(path, drive);
	if (rc)
		return rc;

	rc = _write_cfg_register(drive->ctx, path, cache, LED_IBPI_PATTERN_NONE);
	if (rc)
		return rc;

	return _write_tx_register(drive->ctx, path, &tx_reg);
}

static int _amd_sgpio_init(const char *path, struct led_ctx *ctx)
{
	int rc;
	char em_path[PATH_MAX+10]; /* 10 == strlen("/em_buffer") */
	struct amd_drive drive;
	struct cache_entry *cache;
	struct cache_entry cache_dup;

	memset(&drive, 0, sizeof(struct amd_drive));
	drive.ctx = ctx;

	snprintf(em_path, PATH_MAX+10, "%s/em_buffer", path);

	rc = _get_amd_sgpio_drive(em_path, &drive);
	if (rc) {
		lib_log(ctx, LED_LOG_LEVEL_ERROR,
			"Couldn't find drive info for %s\n", em_path);
		return rc;
	}

	cache = _get_cache(&drive);
	if (!cache) {
		lib_log(ctx, LED_LOG_LEVEL_ERROR,
			"Couldn't retrieve cache");
		return -1;
	}

	/* Save copy of cache entry */
	memcpy(&cache_dup, cache, sizeof(cache_dup));

	rc = _amd_sgpio_init_one(em_path, &drive, cache);
	if (rc) {
		lib_log(ctx, LED_LOG_LEVEL_ERROR,
			"SGPIO register init failed for bank %d, %s", drive.initiator, em_path);

		/* Restore saved cache entry */
		memcpy(cache, &cache_dup, sizeof(*cache));

		goto _init_amd_sgpio_err;
	}

	_put_cache(ctx);

	/* AMD uses SGPIO registers to control drive LEDs in sets of 8
	 * drives. The initiator bit in the amd register controls which
	 * set of four drives (0-3 or 4-7) the transmit register is
	 * updating.
	 *
	 * When initializing the registers we want to do all 8 drives
	 * so we need to reset the drive ata_port and initiator values.
	 */
	if (drive.initiator) {
		drive.ata_port -= 4;
		drive.initiator = 0;
	} else {
		drive.ata_port += 4;
		drive.initiator = 1;
	}

	cache = _get_cache(&drive);
	if (!cache) {
		lib_log(drive.ctx, LED_LOG_LEVEL_ERROR, "Couldn't retrieve cache");
		return -1;
	}

	/* Save copy of cache entry */
	memcpy(&cache_dup, cache, sizeof(cache_dup));

	rc = _amd_sgpio_init_one(em_path, &drive, cache);
	if (rc) {
		lib_log(drive.ctx, LED_LOG_LEVEL_ERROR,
			"SGPIO register init failed for bank %d, %s",
			drive.initiator, em_path);

		/* Restore saved cache entry */
		memcpy(cache, &cache_dup, sizeof(*cache));
	}

_init_amd_sgpio_err:
	_put_cache(ctx);
	return rc;
}

int _amd_sgpio_em_enabled(const char *path, struct led_ctx *ctx)
{
	char *p;
	int rc, found;
	uint32_t caps;
	char em_path[PATH_MAX];
	char buf[BUF_SZ_SM];

	/* Check that libahci module was loaded with ahci_em_messages=1 */
	p = get_text_to_dest("/sys/module/libahci/parameters", "ahci_em_messages",
			     buf, sizeof(buf));
	if (!p || (p && *p == 'N')) {
		lib_log(ctx, LED_LOG_LEVEL_INFO,
			"Kernel libahci module enclosure management messaging not enabled.\n");
		return 0;
	}

	/* Find base path for enclosure management */
	found = _find_file_path(path, "em_buffer", em_path, PATH_MAX, ctx);
	if (!found) {
		lib_log(ctx, LED_LOG_LEVEL_INFO,
			"Couldn't find base EM path for %s\n", path);
		return 0;
	}

	/* Validate that enclosure management is supported */
	p = get_text_to_dest(em_path, "em_message_supported", buf, sizeof(buf));
	if (!p) {
		lib_log(ctx, LED_LOG_LEVEL_INFO,
			"Couldn't get 'em_messages_supported' for %s", path);
		return 0;
	}

	if (strstr(p, "sgpio") == NULL) {
		lib_log(ctx, LED_LOG_LEVEL_INFO, "SGPIO EM not supported for %s\n", path);
		return 0;
	}

	/* Verify host enclosure management capabilities */
	p = get_text_to_dest(em_path, "ahci_host_caps", buf, sizeof(buf));
	if (!p) {
		lib_log(ctx, LED_LOG_LEVEL_INFO,
			"Couldn't read host capabilities for %s\n", path);
		return 0;
	}

	rc = sscanf(p, "%" SCNx32, &caps);

	if (rc <= 0) {
		lib_log(ctx, LED_LOG_LEVEL_INFO,
			"Couldn't parse host capabilities for %s", path);
		return 0;
	}

	if (!(caps & HOST_CAP_EMS)) {
		lib_log(ctx, LED_LOG_LEVEL_INFO,
			"EM not supported for %s", path);
		return 0;
	}

	rc = _amd_sgpio_init(em_path, ctx);
	return rc ? 0 : 1;
}

status_t _amd_sgpio_write(struct block_device *device, enum led_ibpi_pattern ibpi)
{
	/* write only if state has changed */
	if (ibpi == device->ibpi_prev)
		return STATUS_SUCCESS;

	if ((ibpi < LED_IBPI_PATTERN_NORMAL) || (ibpi > LED_IBPI_PATTERN_LOCATE_OFF))
		return STATUS_INVALID_STATE;

	if ((ibpi == LED_IBPI_PATTERN_DEGRADED) ||
	    (ibpi == LED_IBPI_PATTERN_FAILED_ARRAY))
		return STATUS_INVALID_STATE;

	if (_set_ibpi(device, ibpi))
		return STATUS_FILE_WRITE_ERROR;
	return STATUS_SUCCESS;
}

char *_amd_sgpio_get_path(const char *cntrl_path, struct led_ctx *ctx)
{
	int len, found;
	char *em_buffer_path;
	char tmp[PATH_MAX];

	em_buffer_path = malloc(PATH_MAX);
	if (!em_buffer_path) {
		lib_log(ctx, LED_LOG_LEVEL_ERROR,
			"Couldn't allocate memory to get path for %s\n%s",
			cntrl_path, strerror(errno));
		return NULL;
	}

	found = _find_file_path(cntrl_path, "em_buffer", tmp, PATH_MAX, ctx);
	if (!found) {
		lib_log(ctx, LED_LOG_LEVEL_ERROR,
			"Couldn't find EM buffer for %s\n", cntrl_path);
		free(em_buffer_path);
		return NULL;
	}

	len = snprintf(em_buffer_path, PATH_MAX, "%s/em_buffer", tmp);
	if (len < 0 || len >= PATH_MAX) {
		free(em_buffer_path);
		return NULL;
	}

	return em_buffer_path;
}
