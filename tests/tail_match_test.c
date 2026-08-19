// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Red Hat, Inc.

/*
 * Unit tests for tail_block_matches(), the routine that links a RAID member's
 * backing block device to the device ledmon tracks. The interesting case is
 * NVMe multipath, where the member resolves to a virtual namespace-head sysfs
 * path while ledmon tracks the per-controller device under a physical PCI path;
 * the two are reconciled through their shared /dev node.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>

#include "block.h"
#include "tail.h"
#include "utils.h"

static struct block_device make_block(const char *sysfs_path, const char *devnode)
{
	struct block_device bd;

	memset(&bd, 0, sizeof(bd));
	if (sysfs_path)
		snprintf(bd.sysfs_path, PATH_MAX, "%s", sysfs_path);
	if (devnode)
		snprintf(bd.devnode, PATH_MAX, "%s", devnode);
	return bd;
}

/* Split out to keep the full path line within the column limit. */
#define NVME2_PCI_PREFIX "/sys/devices/pci0000:d5/0000:d5:00.5/pci10003:00/10003:00:08.0"
static const char nvme_mp_pci[] =
	NVME2_PCI_PREFIX "/10003:0d:00.0/nvme/nvme2/nvme2c2n1";
static const char nvme_mp_virt[] =
	"/sys/devices/virtual/nvme-subsystem/nvme-subsys2/nvme2n1";
static const char sata_path[] =
	"/sys/devices/pci0000:00/0000:00:1f.2/ata1/host0/target0:0:0/0:0:0:0/block/sda";

struct match_case {
	const char *desc;
	const char *block_sysfs;
	const char *block_devnode;
	const char *member_syspath;
	bool expect;
};

static const struct match_case cases[] = {
	{
		.desc = "exact sysfs path match (non-multipath)",
		.block_sysfs = sata_path,
		.block_devnode = "/dev/sda",
		.member_syspath = sata_path,
		.expect = true,
	},
	{
		.desc = "nvme multipath: virtual head matches per-controller devnode",
		.block_sysfs = nvme_mp_pci,
		.block_devnode = "/dev/nvme2n1",
		.member_syspath = nvme_mp_virt,
		.expect = true,
	},
	{
		.desc = "exact match wins even when devnode is empty",
		.block_sysfs = nvme_mp_virt,
		.block_devnode = "",
		.member_syspath = nvme_mp_virt,
		.expect = true,
	},
	{
		.desc = "different namespace does not match",
		.block_sysfs = nvme_mp_pci,
		.block_devnode = "/dev/nvme2n1",
		.member_syspath =
			"/sys/devices/virtual/nvme-subsystem/nvme-subsys3/nvme3n1",
		.expect = false,
	},
	{
		.desc = "no path match and empty devnode",
		.block_sysfs = nvme_mp_pci,
		.block_devnode = "",
		.member_syspath = nvme_mp_virt,
		.expect = false,
	},
	{
		.desc = "path differs and devnode differs",
		.block_sysfs = sata_path,
		.block_devnode = "/dev/sda",
		.member_syspath = nvme_mp_virt,
		.expect = false,
	},
	{
		.desc = "member path with trailing slash yields no name",
		.block_sysfs = nvme_mp_pci,
		.block_devnode = "/dev/nvme2n1",
		.member_syspath =
			"/sys/devices/virtual/nvme-subsystem/nvme-subsys2/nvme2n1/",
		.expect = false,
	},
};

START_TEST(test_tail_block_matches)
{
	const struct match_case *tc = &cases[_i];
	struct block_device bd = make_block(tc->block_sysfs, tc->block_devnode);
	bool got = tail_block_matches(&bd, tc->member_syspath);

	ck_assert_msg(got == tc->expect,
		      "case '%s': expected %d, got %d", tc->desc, tc->expect, got);
}
END_TEST

static Suite *tail_match_suite(void)
{
	Suite *s = suite_create("tail_match");
	TCase *tc = tcase_create("tail_block_matches");

	tcase_add_loop_test(tc, test_tail_block_matches, 0, (int)ARRAY_SIZE(cases));
	suite_add_tcase(s, tc);
	return s;
}

int main(void)
{
	Suite *s = tail_match_suite();
	SRunner *sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	int number_failed = srunner_ntests_failed(sr);

	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
