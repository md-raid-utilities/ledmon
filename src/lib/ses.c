// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 Intel Corporation.

#include <dirent.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config.h"

#if _HAVE_DMALLOC_H
#include <dmalloc.h>
#endif

#include <scsi/sg_lib.h>
#include <scsi/sg_cmds_extra.h>

#include "ses.h"
#include "libled_private.h"

static int debug = 0;

#define ENCL_CFG_DIAG_STATUS		0x01
#define ENCL_CTRL_DIAG_STATUS		0x02
#define ENCL_CTRL_DIAG_CFG		0x02
#define ENCL_EL_DESCR_STATUS		0x07
#define ENCL_ADDITIONAL_EL_STATUS	0x0a
#define SCSI_PROTOCOL_SAS		6

static int get_ses_page(int fd, struct ses_page *p, int pg_code)
{
	int ret;
	int retry_count = 3;

	do {
		memset(p->buf, 0, sizeof(p->buf));
		ret = sg_ll_receive_diag(fd, 1, pg_code, p->buf, sizeof(p->buf),
					 0, debug);
	} while (ret && retry_count--);

	if (!ret)
		p->len = (p->buf[2] << 8) + p->buf[3] + 4;
	return ret;
}

static int process_page1(struct ses_pages *sp, struct led_ctx *ctx)
{
	int num_encl;		/* number of sub enclosures */
	unsigned char *ed;	/* Enclosure Descriptor */
	int len = 0;
	int sum_headers = 0;	/* Number of Type descriptor headers */
	int i = 0;

	/* How many enclosures is in the main enclosure? */
	num_encl = sp->page1.buf[1] + 1;
	/* Go to Enclosure Descriptor */
	ed = sp->page1.buf + 8;
	for (i = 0; i < num_encl; i++, ed += len) {
		if (ed + 3 > sp->page1.buf + sp->page1.len) {
			lib_log(ctx, LED_LOG_LEVEL_DEBUG,
				"SES: Error, response pare 1 truncated at %d\n", i);
			return 1;
		}
		sum_headers += ed[2];
		len = ed[3] + 4;
		if (len < 40) {
			lib_log(ctx, LED_LOG_LEVEL_DEBUG,
				"SES: Response too short for page 1: %d\n", len);
			continue;
		}
	}

	sp->page1_types = (struct type_descriptor_header *)ed;
	sp->page1_types_len = sum_headers;

	/* ed is on type descr header */
	for (i = 0; i < sum_headers; i++, ed += 4) {
		if (ed > sp->page1.buf + sp->page1.len) {
			lib_log(ctx, LED_LOG_LEVEL_DEBUG,
				"SES: Response page 1 truncated at %d\n", i);
			return 1;
		}
	}
	return 0;
}

static void print_page10(struct ses_pages *sp)
{
	unsigned char *ai = sp->page10.buf + 8;
	int i = 0, len = 0, eip = 0;
	unsigned char *sas = NULL;

	while (ai < sp->page10.buf + sp->page10.len) {
		printf("%s()[%d]: Inv: %d, EIP: %d, Proto: 0x%04x\n", __func__,
		       i++, ((ai[0] & 0x80) >> 7), ((ai[0] & 0x10) >> 4),
		       (unsigned int) (ai[0] & 0xf));
		printf("\tDescriptor len (x-1): %d\n", ai[1] + 1);
		eip = ai[0] & 0x10;
		if (eip)
			printf("\tElement Index: %d EIIOE: %d\n", ai[3], (ai[2] & 0x3));
		len = ai[1] + 2;
		if ((ai[0] & 0xf) == SCSI_PROTOCOL_SAS) {
			if (eip)
				sas = ai + 4;
			else
				sas = ai + 2;
			printf("\tProtocol SAS:\n");
			printf("\tNumber of phy descriptors: %d\n", sas[0]);
			printf("\tNot all phys: %d, descriptor type: 0x%1x\n",
			       (sas[1] & 1), ((sas[1] & 0xc0) >> 6));
			if (eip) {
				printf("\tDevice slot number: %d\n", sas[3]);
				sas += 2;
			}
			sas += 2;
			printf("\tDevice type: 0x%01x\n",
				(unsigned int)((sas[0] & 0x70) >> 4));
			printf("\tSMP Initiator Port: 0x%01x\n",
				(unsigned int)((sas[2] & 2) >> 1));
			printf("\tSTP Initiator Port: 0x%01x\n",
				(unsigned int)((sas[2] & 4) >> 2));
			printf("\tSSP Initiator Port: 0x%01x\n",
				(unsigned int)((sas[2] & 8) >> 3));
			printf("\tSATA DEVICE: 0x%01x\n",
				(unsigned int)(sas[3] & 1));
			printf("\tSMP Target Port: 0x%01x\n",
				(unsigned int)((sas[3] & 2) >> 1));
			printf("\tSTP Target Port: 0x%01x\n",
				(unsigned int)((sas[3] & 4) >> 2));
			printf("\tSSP Target Port: 0x%01x\n",
				(unsigned int)((sas[3] & 8) >> 3));
			printf("\tSATA Port Selector: 0x%01x\n",
				(unsigned int)((sas[3] & 0X80) >> 7));
			printf
			    ("\tAttached SAS Address: 0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
			     sas[4], sas[5], sas[6], sas[7], sas[8], sas[9],
			     sas[10], sas[11]);
			printf
			    ("\tSAS Address: 0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
			     sas[12], sas[13], sas[14], sas[15], sas[16],
			     sas[17], sas[18], sas[19]);
			printf("\tPHY Identified: 0x%01x\n", sas[20]);
		} else
			printf("\tProtocol not SAS: 0x%02x, skipping\n",
				(unsigned int)(ai[0] & 0xf));
		/* */
		ai += len;
	}
	return;
}

int ses_load_pages(int fd, struct ses_pages *sp, struct led_ctx *ctx)
{
	int ret;

	/* Read configuration. */
	ret = get_ses_page(fd, &sp->page1, ENCL_CFG_DIAG_STATUS);
	if (ret)
		return ret;

	ret = process_page1(sp, ctx);
	if (ret)
		return ret;

	/* Get Enclosure Status */
	ret = get_ses_page(fd, &sp->page2, ENCL_CTRL_DIAG_STATUS);
	if (ret)
		return ret;

	/* Additional Element Status */
	ret = get_ses_page(fd, &sp->page10, ENCL_ADDITIONAL_EL_STATUS);
	if (ret)
		return ret;

	if (debug)
		print_page10(sp);

	return ret;
}

static enum led_ibpi_pattern ibpi_to_ses(enum led_ibpi_pattern ibpi)
{
	switch (ibpi) {
	case LED_IBPI_PATTERN_UNKNOWN:
	case LED_IBPI_PATTERN_ONESHOT_NORMAL:
	case LED_IBPI_PATTERN_NORMAL:
		return LED_SES_REQ_OK;
	case LED_IBPI_PATTERN_FAILED_ARRAY:
		return LED_SES_REQ_IFA;
	case LED_IBPI_PATTERN_DEGRADED:
		return LED_SES_REQ_ICA;
	case LED_IBPI_PATTERN_REBUILD:
		return LED_SES_REQ_REBUILD;
	case LED_IBPI_PATTERN_FAILED_DRIVE:
		return LED_SES_REQ_FAULT;
	case LED_IBPI_PATTERN_LOCATE:
		return LED_SES_REQ_IDENT;
	case LED_IBPI_PATTERN_HOTSPARE:
		return LED_SES_REQ_HOTSPARE;
	case LED_IBPI_PATTERN_PFA:
		return LED_SES_REQ_PRDFAIL;
	case LED_IBPI_PATTERN_LOCATE_AND_FAIL:
		return LED_SES_REQ_IDENT_AND_FAULT;
	default:
		return ibpi;
	}
}

static inline void _set_prdfail(unsigned char *u)
{
	u[0] |= (1 << 6);
}

static inline void _set_abrt(unsigned char *u)
{
	u[1] |= (1 << 0);
}

static inline void _set_rebuild(unsigned char *u)
{
	u[1] |= (1 << 1);
}

static inline void _set_ifa(unsigned char *u)
{
	u[1] |= (1 << 2);
}

static inline void _set_ica(unsigned char *u)
{
	u[1] |= (1 << 3);
}

static inline void _set_cons_check(unsigned char *u)
{
	u[1] |= (1 << 4);
}

static inline void _set_hspare(unsigned char *u)
{
	u[1] |= (1 << 5);
}

static inline void _set_rsvd_dev(unsigned char *u)
{
	u[1] |= (1 << 6);
}

static inline void _set_ok(unsigned char *u)
{
	u[1] |= (1 << 7);
}

static inline void _set_ident(unsigned char *u)
{
	u[2] |= (1 << 1);
}

static inline void _clr_ident(unsigned char *u)
{
	u[2] &= ~(1 << 1);
}

static inline void _set_rm(unsigned char *u)
{
	u[2] |= (1 << 2);
}

static inline void _set_ins(unsigned char *u)
{
	u[2] |= (1 << 3);
}

static inline void _set_miss(unsigned char *u)
{
	u[2] |= (1 << 4);
}

static inline void _set_dnr(unsigned char *u)
{
	u[2] |= (1 << 6);
}

static inline void _set_actv(unsigned char *u)
{
	u[2] |= (1 << 7);
}

static inline void _set_enbb(unsigned char *u)
{
	u[3] |= (1 << 2);
}

static inline void _set_enba(unsigned char *u)
{
	u[3] |= (1 << 3);
}

static inline void _set_off(unsigned char *u)
{
	u[3] |= (1 << 4);
}

static inline void _set_fault(unsigned char *u)
{
	u[3] |= (1 << 5);
}

static int ses_set_message(enum led_ibpi_pattern ibpi, struct ses_slot_ctrl_elem *el)
{
	struct ses_slot_ctrl_elem msg;

	memset(&msg, 0, sizeof(msg));
	if (ibpi == LED_IBPI_PATTERN_LOCATE_OFF) {
		/*
		 * For locate_off we don't set a new state, just clear the
		 * IDENT bit and the bits that are reserved or have different
		 * meanings in Status and Control pages (RQST ACTIVE and
		 * RQST MISSING).
		 */
		_clr_ident(el->b);
		el->b2 &= 0x4e;
		el->b3 &= 0x3c;
		return 0;
	}

	switch (ibpi_to_ses(ibpi)) {
	case LED_SES_REQ_ABORT:
		_set_abrt(msg.b);
		break;
	case LED_SES_REQ_REBUILD:
		_set_rebuild(msg.b);
		break;
	case LED_SES_REQ_IFA:
		_set_ifa(msg.b);
		break;
	case LED_SES_REQ_ICA:
		_set_ica(msg.b);
		break;
	case LED_SES_REQ_CONS_CHECK:
		_set_cons_check(msg.b);
		break;
	case LED_SES_REQ_HOTSPARE:
		_set_hspare(msg.b);
		break;
	case LED_SES_REQ_RSVD_DEV:
		_set_rsvd_dev(msg.b);
		break;
	case LED_SES_REQ_OK:
		_set_ok(msg.b);
		break;
	case LED_SES_REQ_IDENT:
		_set_ident(msg.b);
		break;
	case LED_SES_REQ_RM:
		_set_rm(msg.b);
		break;
	case LED_SES_REQ_INS:
		_set_ins(msg.b);
		break;
	case LED_SES_REQ_MISSING:
		_set_miss(msg.b);
		break;
	case LED_SES_REQ_DNR:
		_set_dnr(msg.b);
		break;
	case LED_SES_REQ_ACTIVE:
		_set_actv(msg.b);
		break;
	case LED_SES_REQ_EN_BB:
		_set_enbb(msg.b);
		break;
	case LED_SES_REQ_EN_BA:
		_set_enba(msg.b);
		break;
	case LED_SES_REQ_DEV_OFF:
		_set_off(msg.b);
		break;
	case LED_SES_REQ_FAULT:
		_set_fault(msg.b);
		break;
	case LED_SES_REQ_PRDFAIL:
		_set_prdfail(msg.b);
		break;
	case LED_SES_REQ_IDENT_AND_FAULT:
		_set_ident(msg.b);
		_set_fault(msg.b);
		break;
	default:
		return 1;
	}

	*el = msg;

	return 0;
}

status_t ses_write_msg(enum led_ibpi_pattern ibpi, struct ses_pages *sp, int idx)
{
	/* Move do descriptors */
	struct ses_slot_ctrl_elem *descriptors = (void *)(sp->page2.buf + 8);
	int i;
	struct ses_slot_ctrl_elem *desc_element = NULL;
	element_type local_element_type = SES_UNSPECIFIED;

	for (i = 0; i < sp->page1_types_len; i++) {
		const struct type_descriptor_header *t = &sp->page1_types[i];

		descriptors++; /* At first, skip overall header. */

		if (t->element_type == SES_DEVICE_SLOT ||
		    t->element_type == SES_ARRAY_DEVICE_SLOT) {
			if (local_element_type < t->element_type &&
			    t->num_of_elements > idx) {
				local_element_type = t->element_type;
				desc_element = &descriptors[idx];
			}
		} else {
			/*
			 * Device Slot and Array Device Slot elements are
			 * always first on the type descriptor header list
			 */
			break;
		}

		descriptors += t->num_of_elements;
	}

	if (desc_element) {
		int ret = ses_set_message(ibpi, desc_element);
		if (ret)
			return STATUS_INVALID_STATE;

		sp->changes++;

		/* keep PRDFAIL, clear rest */
		desc_element->common_control &= 0x40;
		/* set select */
		desc_element->common_control |= 0x80;

		/* second byte is valid only for Array Device Slot */
		if (local_element_type != SES_ARRAY_DEVICE_SLOT)
			desc_element->array_slot_control = 0;

		return STATUS_SUCCESS;
	}

	return STATUS_FILE_WRITE_ERROR;
}

int ses_send_diag(int fd, struct ses_pages *sp)
{
	return sg_ll_send_diag(fd, 0, 1, 0, 0, 0, 0, sp->page2.buf,
			       sp->page2.len, 0, debug);
}

static void get_led_status(struct ses_pages *sp, int idx, enum led_ibpi_pattern *led_status)
{
	struct ses_slot_ctrl_elem *descriptors = (void *)(sp->page2.buf + 8);
	struct ses_slot_ctrl_elem *desc_element = NULL;
	element_type ele_type = sp->page1_types[0].element_type;
	descriptors++;
	desc_element = &descriptors[idx];

	*led_status = LED_IBPI_PATTERN_NORMAL;

	if ((desc_element->b2 & 0x02) && (desc_element->b3 & 0x60))
		*led_status = LED_IBPI_PATTERN_LOCATE_AND_FAIL;
	else if (desc_element->b2 & 0x02)
		*led_status = LED_IBPI_PATTERN_LOCATE;
	else if (desc_element->b3 & 0x60)
		*led_status = LED_IBPI_PATTERN_FAILED_DRIVE;
	else if (desc_element->common_control & 0x40)
		*led_status = LED_IBPI_PATTERN_PFA;

	// Byte 1 is only valid for Array Device Slot elements
	if ((*led_status == LED_IBPI_PATTERN_NORMAL) && (ele_type == SES_ARRAY_DEVICE_SLOT))
	{
		if (desc_element->array_slot_control & 0x04)
			*led_status = LED_IBPI_PATTERN_FAILED_ARRAY;
		else if (desc_element->array_slot_control & 0x02)
			*led_status = LED_IBPI_PATTERN_REBUILD;
		else if (desc_element->array_slot_control & 0x20)
			*led_status = LED_IBPI_PATTERN_HOTSPARE;
		else if (desc_element->array_slot_control & 0x08)
			*led_status = LED_IBPI_PATTERN_DEGRADED;
		else if (desc_element->array_slot_control & 0x10)
			*led_status = LED_SES_REQ_CONS_CHECK;
		else if (desc_element->array_slot_control & 0x01)
			*led_status = LED_SES_REQ_ABORT;
		else if (desc_element->array_slot_control & 0x40)
			*led_status = LED_SES_REQ_RSVD_DEV;
	}

	if (*led_status == LED_IBPI_PATTERN_NORMAL)
	{
		if (desc_element->b2 & 0x40)
			*led_status = LED_SES_REQ_DNR;
		else if (desc_element->b2 & 0x08)
			*led_status = LED_SES_REQ_INS;
		else if (desc_element->b2 & 0x04)
			*led_status = LED_SES_REQ_RM;
		else if (desc_element->b3 & 0x10)
			*led_status = LED_SES_REQ_DEV_OFF;
		else if (desc_element->b3 & 0x08)
			*led_status = LED_SES_REQ_EN_BA;
		else if (desc_element->b3 & 0x04)
			*led_status = LED_SES_REQ_EN_BB;
	}
}

int ses_get_slots(struct ses_pages *sp, struct ses_slot **out_slots, int *out_slots_count)
{
	unsigned char *ap = NULL, *addr_p = NULL;
	int i, j, len = 0;

	/* Check Page10 for address. Extract index. */
	ap = sp->page10.buf + 8;
	for (i = 0; i < sp->page1_types_len; i++) {
		const struct type_descriptor_header *t = &sp->page1_types[i];

		if (t->element_type == SES_DEVICE_SLOT ||
		    t->element_type == SES_ARRAY_DEVICE_SLOT) {
			struct ses_slot *slots;

			slots = calloc(t->num_of_elements, sizeof(*slots));
			if (!slots)
				return -1;

			for (j = 0; j < t->num_of_elements; j++, ap += len) {
				/* Get Additional Element Status Descriptor */
				/* length (x-1) */
				len = ap[1] + 2;
				if ((ap[0] & 0xf) != SCSI_PROTOCOL_SAS) {
					slots[j].index = -1;
					continue;	/* need SAS PROTO */
				}
				/* It is a SAS protocol, go on */
				if ((ap[0] & 0x10))	/* Check EIP */
					addr_p = ap + 8;
				else
					addr_p = ap + 4;
				/* Process only PHY 0 descriptor. */

				/* Convert be64 to le64 */
				slots[j].sas_addr =
					((uint64_t)addr_p[12] << 8*7) |
					((uint64_t)addr_p[13] << 8*6) |
					((uint64_t)addr_p[14] << 8*5) |
					((uint64_t)addr_p[15] << 8*4) |
					((uint64_t)addr_p[16] << 8*3) |
					((uint64_t)addr_p[17] << 8*2) |
					((uint64_t)addr_p[18] << 8*1) |
					((uint64_t)addr_p[19]);

				slots[j].index = ap[0] & 0x10 ? ap[3] : j;

				// If EIIOE=1, ELEMENT INDEX includes overall elements, otherwise not.
				// As (Array) Device Slot elements are always first in the list, there
				// will be exactly one overall element that needs to be ignored.
				if ((ap[0] & 0x10) && ((ap[2] & 0x3) == 0x01))
					slots[j].index--;

				get_led_status(sp, slots[j].index, &slots[j].ibpi_status);
			}

			// Our expectation is that the caller is passing us NULL or a valid chunk of
			// memory for slots that was used before and are now reloading, so free it first.
			free(*out_slots);
			*out_slots = slots;
			*out_slots_count = t->num_of_elements;

			return 0;
		}
	}

	return 1;
}

uint64_t ses_get_primary_logical_id(struct ses_pages *sp)
{
	/*
	 * SES page 1 spec. states
	 * "The primary subenclosure shall be described by the first enclosure descriptor"
	 *
	 * The first enclosure is located at byte 8 in page 1 and the enclosure logical identifier
	 * is an additional 4 bytes offset in the enclosure descriptor (bytes 4-11).
	 */
	uint64_t lid = be64toh(*(uint64_t *)&sp->page1.buf[8 + 4]);
	uint8_t naa_type = lid >> 60;  /* upper 4 bits is naa type */

	/*
	 * Do a simple validity check based on the specification. These are the only NAA types that
	 * are 8 bytes in size. The 0x06 NAA identifier is 16 bytes in size.
	 */
	if (naa_type == 0x2 || naa_type == 0x3 || naa_type == 0x05)
		return lid;

	return 0;
}
