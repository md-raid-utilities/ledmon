// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Red Hat, Inc.

/*
 * Unit tests for block_path_has_subpath(), the routine that decides whether a
 * block device's sysfs path sits under a slot or controller. The interesting
 * case is a PCI slot address that is a suffix of a longer PCI domain, e.g.
 * slot "0000:01:00" and a drive under VMD domain "10000:01:00.0".
 */

#include <stdbool.h>
#include <stdlib.h>
#include <check.h>

#include "block.h"
#include "utils.h"

#define VMD_ROOT_PORT "/sys/devices/pci0000:00/0000:00:0e.0/pci10000:00/10000:00:02.0"

static const char vmd_nvme[] = VMD_ROOT_PORT "/10000:01:00.0/nvme/nvme0/nvme0n1";
static const char pci_nvme[] =
	"/sys/devices/pci0000:00/0000:00:1c.0/0000:01:00.0/nvme/nvme1/nvme1n1";

struct match_case {
	const char *desc;
	const char *path;
	const char *sub_path;
	bool sub_path_to_end;
	bool expect;
};

static const struct match_case cases[] = {
	{
		.desc = "slot address matches its own function",
		.path = vmd_nvme,
		.sub_path = "10000:01:00",
		.sub_path_to_end = true,
		.expect = true,
	},
	{
		.desc = "slot address does not match inside a longer domain",
		.path = vmd_nvme,
		.sub_path = "0000:01:00",
		.sub_path_to_end = true,
		.expect = false,
	},
	{
		.desc = "domain 0 slot address matches domain 0 drive",
		.path = pci_nvme,
		.sub_path = "0000:01:00",
		.sub_path_to_end = true,
		.expect = true,
	},
	{
		.desc = "later anchored occurrence matches after an unanchored one",
		.path = "/sys/devices/x/10000:01:00.0/0000:01:00.0/nvme/nvme2/nvme2n1",
		.sub_path = "0000:01:00",
		.sub_path_to_end = true,
		.expect = true,
	},
	{
		.desc = "controller path is a prefix of the device path",
		.path = pci_nvme,
		.sub_path = "/sys/devices/pci0000:00/0000:00:1c.0",
		.sub_path_to_end = true,
		.expect = true,
	},
	{
		.desc = "complete component required when not matching to end",
		.path = pci_nvme,
		.sub_path = "0000:01:00",
		.sub_path_to_end = false,
		.expect = false,
	},
	{
		.desc = "complete component followed by '/'",
		.path = pci_nvme,
		.sub_path = "0000:01:00.0",
		.sub_path_to_end = false,
		.expect = true,
	},
	{
		.desc = "complete component at end of path",
		.path = pci_nvme,
		.sub_path = "nvme1n1",
		.sub_path_to_end = false,
		.expect = true,
	},
	{
		.desc = "empty sub path never matches",
		.path = pci_nvme,
		.sub_path = "",
		.sub_path_to_end = true,
		.expect = false,
	},
};

START_TEST(test_block_path_has_subpath)
{
	const struct match_case *tc = &cases[_i];
	bool got = block_path_has_subpath(tc->path, tc->sub_path, tc->sub_path_to_end);

	ck_assert_msg(got == tc->expect,
		      "case '%s': expected %d, got %d", tc->desc, tc->expect, got);
}
END_TEST

/*
 * A device whose controller was hot-removed is left on the tracked list with a
 * NULL cntrl by _revalidate_dev() until the scan reaps it, but _add_block()
 * still runs block_compare() against it first. block_compare() must treat a
 * missing controller as "no match" rather than dereferencing it (this crashed
 * ledmon on a real NVMe PCIe hot-remove).
 */
START_TEST(test_block_compare_null_cntrl)
{
	struct cntrl_device cntrl = { 0 };
	struct block_device present = { .cntrl = &cntrl };
	struct block_device removed = { .cntrl = NULL };

	ck_assert_int_eq(block_compare(&removed, &present), 0);
	ck_assert_int_eq(block_compare(&present, &removed), 0);
	ck_assert_int_eq(block_compare(&removed, &removed), 0);
}
END_TEST

static Suite *block_match_suite(void)
{
	Suite *s = suite_create("block_match");
	TCase *tc = tcase_create("block_path_has_subpath");

	tcase_add_loop_test(tc, test_block_path_has_subpath, 0, (int)ARRAY_SIZE(cases));
	tcase_add_test(tc, test_block_compare_null_cntrl);
	suite_add_tcase(s, tc);
	return s;
}

int main(void)
{
	Suite *s = block_match_suite();
	SRunner *sr = srunner_create(s);
	int number_failed;

	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);

	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
