// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023 Red Hat, Inc.

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <check.h>
#include <errno.h>
#include <led/libled.h>

/* Global context which is initialized in setup and free'd in teardown */
struct led_ctx *ctx;


/* for filtering slots, for known issues with testing hardware */
#define MAX_FILTERED_SIZE 6
char *slot_filters[MAX_FILTERED_SIZE] = {NULL, NULL, NULL, NULL, NULL, NULL};



void setup(void)
{
	led_status_t status = led_new(&ctx);

	if (status != LED_STATUS_SUCCESS) {
		printf("%s: led_new %u", __func__, status);
		exit(EXIT_FAILURE);
	}
	status = led_scan(ctx);
	if (status != LED_STATUS_SUCCESS) {
		printf("%s: led_scan %u", __func__, status);
		exit(EXIT_FAILURE);
	}
}

void teardown(void)
{
	led_status_t status = led_free(ctx);

	if (status != LED_STATUS_SUCCESS) {
		printf("%s: led_free %u", __func__, status);
		exit(EXIT_FAILURE);
	}
}

void setup_device_filters(void)
{
	int num_filters = 0;
	char *found = NULL;
	char *tmp_filters = getenv("LEDMONTEST_SLOT_FILTER");

	if (tmp_filters) {
		// Store a variable to where it was allocated so we can free, strsep will modify
		// s_dupe address.
		char *a_dupe = strdup(tmp_filters);
		char *s_dupe = a_dupe;

		printf("slot filter = ");
		while ((found = strsep(&s_dupe, ",")) && (num_filters < MAX_FILTERED_SIZE - 1)) {
			slot_filters[num_filters] = strdup(found);
			printf("%s ", slot_filters[num_filters]);
			num_filters += 1;
		}
		free(a_dupe);
		printf("\n");
	}
}

bool slot_usable(const char *slot_id)
{
	char **t = slot_filters;

	while (*t) {
		if (strcasestr(slot_id, *t))
			return false;
		t += 1;
	}
	return true;
}

void free_device_filters(void)
{
	char **t = slot_filters;

	while (*t) {
		free(*t);
		t += 1;
	}
}

void block_device_check(const char *path)
{
	struct stat block_dev;
	int rc;

	if (!path)
		return;

	rc = stat(path, &block_dev);
	if (rc != 0) {
		int errno_cpy = errno;

		ck_assert_msg(rc == 0,
				"stat failed for %s, errno: %s", path, strerror(errno_cpy));
		return;
	}

	ck_assert_msg((block_dev.st_mode & S_IFMT) == S_IFBLK,
			"Expecting block device 0x%x block_dev.st_mode", block_dev.st_mode);
}

START_TEST(test_load_unload)
{
	// Simply create a context and free it when we already have one created in setup/teardown
	struct led_ctx *lctx = NULL;
	led_status_t status = led_new(&lctx);

	ck_assert_msg(status == LED_STATUS_SUCCESS, "led_new = %u", status);

	status = led_scan(lctx);
	ck_assert_msg(status == LED_STATUS_SUCCESS, "led_scan = %u", status);

	status = led_free(lctx);
	ck_assert_msg(status == LED_STATUS_SUCCESS, "led_free = %u", status);
}
END_TEST

START_TEST(test_list_controllers)
{
	// Test listing controllers
	struct led_cntrl_list *cl = NULL;
	led_status_t status = led_cntrls_get(ctx, &cl);

	ck_assert_msg(status == LED_STATUS_SUCCESS, "led_cntrls_get %u", status);
	bool devices_found = false;

	if (status == LED_STATUS_SUCCESS) {
		struct led_cntrl_list_entry *ce;

		while ((ce = led_cntrl_next(cl))) {
			ck_assert_msg(led_cntrl_path(ce) != NULL, "led_cntrl_path returned NULL");
			enum led_cntrl_type t = led_cntrl_type(ce);

			devices_found = true;
			ck_assert_msg(((int)t >= 1 && (int)t <= 7),  "invalid %u cntrl type", t);
		}

		led_cntrl_list_reset(cl);
		while ((ce = led_cntrl_prev(cl))) {
			ck_assert_msg(led_cntrl_path(ce) != NULL, "led_cntrl_path returned NULL");
			enum led_cntrl_type t = led_cntrl_type(ce);

			ck_assert_msg(((int)t >= 1 && (int)t <= 7),  "invalid %u cntrl type", t);
		}

		led_cntrl_list_free(cl);
	}

	ck_assert_msg(devices_found, "No test LED devices found!");
}
END_TEST

START_TEST(test_list_slots)
{
	// Test listing slots
	struct led_slot_list *sl = NULL;
	bool devices_found = false;
	led_status_t status = led_slots_get(ctx, &sl);

	ck_assert_msg(status == LED_STATUS_SUCCESS, "led_slots_get %u", status);
	if (status == LED_STATUS_SUCCESS) {
		struct led_slot_list_entry *se;

		while ((se = led_slot_next(sl))) {
			ck_assert_msg(led_slot_id(se) != NULL, "led_slot_id returned NULL");
			enum led_cntrl_type t = led_slot_cntrl(se);

			ck_assert_msg(((int)t >= 1 && (int)t <= 7),  "invalid %u cntrl type", t);
			devices_found = true;
			// TODO: Check led ibpi pattern and validate

			block_device_check(led_slot_device(se));
		}

		led_slot_list_reset(sl);
		while ((se = led_slot_prev(sl))) {
			ck_assert_msg(led_slot_id(se) != NULL, "led_slot_id returned NULL");
			enum led_cntrl_type t = led_slot_cntrl(se);

			ck_assert_msg(((int)t >= 1 && (int)t <= 7),  "invalid %u cntrl type", t);
			// TODO: Check led ibpi pattern and validate

			block_device_check(led_slot_device(se));
		}

		led_slot_list_free(sl);
	}
	ck_assert_msg(devices_found, "No test LED devices found!");
}
END_TEST


START_TEST(test_toggle_slots)
{
	// Test toggling slots
	struct led_slot_list *sl = NULL;
	bool devices_found = false;
	led_status_t status = led_slots_get(ctx, &sl);

	ck_assert_msg(status == LED_STATUS_SUCCESS, "led_slots_get %u", status);
	if (status == LED_STATUS_SUCCESS) {
		struct led_slot_list_entry *se;

		while ((se = led_slot_next(sl))) {
			if (led_controller_slot_support(led_slot_cntrl(se)) &&
				slot_usable(led_slot_id(se))) {
				enum led_ibpi_pattern led = led_slot_state(se);

				led = (led == LED_IBPI_PATTERN_NORMAL) ?
					LED_IBPI_PATTERN_LOCATE : LED_IBPI_PATTERN_NORMAL;
				status = led_slot_set(ctx, se, led);
				devices_found = true;
				ck_assert_msg(status == LED_STATUS_SUCCESS,
						"led_slot_set %u", status);
				enum led_ibpi_pattern after_set = led_slot_state(se);

				ck_assert_msg(led == after_set,
						"%s led_slot_state expected = (%u) != actual (%u)",
						led_slot_id(se), led, after_set);
			}
		}
		led_slot_list_free(sl);
	}
	ck_assert_msg(devices_found, "No test LED devices found!");
}
END_TEST


START_TEST(test_led_by_path)
{
	// Test led using device node path
	struct led_slot_list *sl = NULL;
	bool devices_found = false;
	led_status_t status = led_slots_get(ctx, &sl);

	ck_assert_msg(status == LED_STATUS_SUCCESS, "led_slots_get %u", status);
	if (status == LED_STATUS_SUCCESS) {
		struct led_slot_list_entry *se;

		while ((se = led_slot_next(sl))) {
			const char *device_node = led_slot_device(se);

			if (led_controller_slot_support(led_slot_cntrl(se)) &&
				slot_usable(led_slot_id(se)) && device_node) {
				char normalized[PATH_MAX];
				enum led_ibpi_pattern led_via_slot;
				enum led_ibpi_pattern expected;

				status = led_device_name_lookup(ctx, device_node, normalized);
				ck_assert_msg(status == LED_STATUS_SUCCESS,
						"led_device_name_lookup %u", status);
				if (status != LED_STATUS_SUCCESS)
					return;

				if (led_is_management_supported(ctx, normalized) !=
					led_slot_cntrl(se))
					continue;

				devices_found = true;

				led_via_slot = led_slot_state(se);
				expected = (led_via_slot == LED_IBPI_PATTERN_NORMAL) ?
					LED_IBPI_PATTERN_LOCATE : LED_IBPI_PATTERN_NORMAL;

				status = led_set(ctx, normalized, expected);
				ck_assert_msg(status == LED_STATUS_SUCCESS, "led_set %u", status);
				if (status != LED_STATUS_SUCCESS)
					return;

				led_flush(ctx);

				led_via_slot = led_slot_state(se);

				ck_assert_msg(expected == led_via_slot,
						"Retrieved state %u != %u expected",
						led_via_slot, expected);
				if (expected != led_via_slot)
					return;
			}
		}
		led_slot_list_free(sl);
	}
	ck_assert_msg(devices_found, "No test LED devices found!");
}
END_TEST


START_TEST(test_persistent_id)
{
	/* Test persistent ID functionality */
	struct led_slot_list *sl = NULL;
	bool scsi_controller_found = false;
	bool persistent_id_found = false;
	led_status_t status = led_slots_get(ctx, &sl);

	ck_assert_msg(status == LED_STATUS_SUCCESS, "led_slots_get %u", status);
	if (status == LED_STATUS_SUCCESS) {
		struct led_slot_list_entry *se;

		while ((se = led_slot_next(sl))) {
			enum led_cntrl_type cntrl_type = led_slot_cntrl(se);
			bool persistent_support = led_slot_persistent_id_support(ctx, cntrl_type);
			const char *persistent_id = led_slot_persistent_id(se);
			const char *slot_id = led_slot_id(se);

			/*
			 * Test that persistent ID support function behaves
			 * correctly for all controller types
			 */
			if (cntrl_type == LED_CNTRL_TYPE_SCSI) {
				scsi_controller_found = true;
				ck_assert_msg(persistent_support == true,
						"SCSI controller should support persistent IDs");

				/*
				 * For SCSI controllers, persistent ID may be NULL
				 * if hardware doesn't implement it
				 */
				if (persistent_id != NULL) {
					persistent_id_found = true;
					ck_assert_msg(strlen(persistent_id) > 0,
						"Persistent ID empty string for slot %s", slot_id);
					/* Persistent ID should be different from regular slot ID */
					ck_assert_msg(strcmp(persistent_id, slot_id) != 0,
						"Persistent ID = slot ID for slot %s", slot_id);
				}
			} else {
				/* For non-SCSI controllers, should not support persistent IDs */
				ck_assert_msg(persistent_support == false,
					"Cntrl %u, should not support persistent ID", cntrl_type);
				ck_assert_msg(persistent_id == NULL,
					"Persistent ID != NULL for slot ID %s", slot_id);
			}
		}
		led_slot_list_free(sl);
	}

	/* Report what we found for debugging purposes */
	printf("Test summary: SCSI controllers found: %s, Persistent IDs found: %s\n",
		   scsi_controller_found ? "yes" : "no",
		   persistent_id_found ? "yes" : "no");
}
END_TEST


Suite *led_lib_suite(void)
{
	// We could use the setup/teardown functions to create/free the ctx before each test.
	Suite *s = suite_create("ledmon");
	TCase *tc_main = tcase_create("lib_unit_test");

	tcase_set_timeout(tc_main, 0);	/* Disable timeouts */
	tcase_add_checked_fixture(tc_main, setup, teardown);
	tcase_add_test(tc_main, test_load_unload);
	tcase_add_test(tc_main, test_list_controllers);
	tcase_add_test(tc_main, test_list_slots);
	tcase_add_test(tc_main, test_toggle_slots);
	tcase_add_test(tc_main, test_led_by_path);
	tcase_add_test(tc_main, test_persistent_id);
	suite_add_tcase(s, tc_main);
	return s;
}


int main(void)
{
	Suite *s = led_lib_suite();
	SRunner *sr = srunner_create(s);

	setup_device_filters();
	srunner_run_all(sr, CK_NORMAL);
	int number_failed = srunner_ntests_failed(sr);

	srunner_free(sr);
	free_device_filters();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
