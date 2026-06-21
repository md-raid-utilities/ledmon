// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2017 Intel Corporation.
// Copyright (C) 2009 Karel Zak <kzak@redhat.com>

/*
 * Contains code from util-linux/libblkid/src/config.c
 * originally released under LGPL.
 */


#ifndef SRC_CONFIG_FILE_H_
#define SRC_CONFIG_FILE_H_

#include <stdio.h>

#include <lib/list.h>
#include <led/libled.h>

#define LEDMON_SHARE_MEM_FILE "/ledmon.conf"
#define LEDMON_DEF_CONF_FILE "/etc/ledmon.conf"
#define LEDMON_DEF_LOG_FILE "/var/log/ledmon.log"
#define LEDCTL_DEF_LOG_FILE "/var/log/ledctl.log"
#define LEDMON_DEF_SLEEP_INTERVAL 10
#define LEDMON_MIN_SLEEP_INTERVAL 5

struct ledmon_conf {
	/* internal ledmon functions */
	FILE *s_log;
	char *log_path;
	enum led_log_level_enum log_level;
	int scan_interval;

	/* customizable leds behaviour */
	int blink_on_migration;
	int blink_on_init;
	int rebuild_blink_on_all;
	int raid_members_only;
	int blink_persistent_fail_on_readd;

	/* allowlist and excludelist of controllers for blinking */
	struct list cntrls_allowlist;
	struct list cntrls_excludelist;
};

int ledmon_read_conf(const char *filename, struct ledmon_conf *conf);
int ledmon_write_shared_conf(struct ledmon_conf *conf);
int ledmon_remove_shared_conf(void);


int ledmon_init_conf(struct ledmon_conf *conf, enum led_log_level_enum lvl, const char *log_path);
void ledmon_free_conf(struct ledmon_conf *conf);

#endif /* SRC_CONFIG_FILE_H_ */
