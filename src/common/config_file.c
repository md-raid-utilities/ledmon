// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2017 Intel Corporation.
// Copyright (C) 2009 Karel Zak <kzak@redhat.com>

/*
 * Contains code from util-linux/libblkid/src/config.c
 * originally released under LGPL.
 */

/* System headers */
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

/* Local headers */
#include "lib/status.h"
#include "lib/utils.h"
#include "config_file.h"


/**
 * @brief Pointer to global ledmon configuration.
 *
 * This is a pointer to structure that contains settings of ledmon behavior
 * read from configuration file.
 */

const char *log_level_map[] = {
	[LED_LOG_LEVEL_QUIET]   = "QUIET",
	[LED_LOG_LEVEL_ERROR]   = "ERROR",
	[LED_LOG_LEVEL_WARNING] = "WARNING",
	[LED_LOG_LEVEL_INFO]    = "INFO",
	[LED_LOG_LEVEL_DEBUG]   = "DEBUG",
	[LED_LOG_LEVEL_ALL]     = "ALL"
};

int ledmon_init_conf(struct ledmon_conf *conf, enum led_log_level_enum lvl, const char *log_path)
{
	memset(conf, 0, sizeof(struct ledmon_conf));
	conf->log_level = lvl;
	list_init(&conf->cntrls_allowlist, free);
	list_init(&conf->cntrls_excludelist, free);

	return set_log_path(conf, log_path);
}

static int parse_bool(char *s)
{
	if (*s && (!strcasecmp(s, "enabled") ||
		   !strcasecmp(s, "true") ||
		   !strcasecmp(s, "yes") ||
		   !strcasecmp(s, "1")))
		return 1;
	else if (*s && (!strcasecmp(s, "disabled") ||
		       !strcasecmp(s, "false") ||
		       !strcasecmp(s, "no") ||
		       !strcasecmp(s, "0")))
		return 0;

	fprintf(stderr, "Unknown bool value: %s\n", s);
	return -1;
}

static bool parse_list(struct list *list, char *s)
{
	char *copy = NULL;
	list_erase(list);

	while (s && *s) {
		char *sep;

		sep = strchr(s, ',');
		if (sep)
			*sep = '\0';

		copy = strdup(s);
		if (copy) {
			if (!list_append(list, copy)) {
				fprintf(stderr, "%s: memory allocation error!", __func__);
				return false;
			}
		} else
			return false;

		if (sep)
			s = sep + 1;
		else
			break;
	}
	return true;
}

static bool _parse_and_add_to_list(char *s, const char *key, size_t key_len, struct list *list)
{
	if (strncmp(s, key, key_len) != 0)
		return false;

	s += key_len;

	if (*s)
		parse_list(list, s);
	return true;
}

int _map_log_level(char *conf_log_level)
{
	size_t i = 1;

	while (i < sizeof(log_level_map)/sizeof(char *)) {
		if (strcasecmp(log_level_map[i], conf_log_level) == 0)
			return i;
		i++;
	}
	return 0;
}

void _set_log_level(struct ledmon_conf *conf, char *s)
{
	int log_level;

	log_level = _map_log_level(s);
	if (log_level)
		conf->log_level = log_level;
	else if (sscanf(s, "%d", &log_level) == 1 &&
			log_level >= LED_LOG_LEVEL_QUIET &&
			log_level <= LED_LOG_LEVEL_ALL)
		conf->log_level = log_level;
	else
		_log(conf, LED_LOG_LEVEL_WARNING,
		    "Log level given in config file (%s) is incorrect! Using default log level: %s",
		    s, log_level_map[conf->log_level]);
}

#define KEYLEN(key)   (sizeof((key))- 1)	/* Subtract 1 for string terminator */

#define ALLOWLIST "ALLOWLIST="
#define ALLOWLIST_LEN KEYLEN(ALLOWLIST)
#define EXCLUDELIST "EXCLUDELIST="
#define EXCLUDELIST_LEN KEYLEN(EXCLUDELIST)

#define WHITELIST "WHITELIST="
#define WHITELIST_LEN KEYLEN(WHITELIST)
#define BLACKLIST "BLACKLIST="
#define BLACKLIST_LEN KEYLEN(BLACKLIST)

static int parse_next(FILE *fd, struct ledmon_conf *conf)
{
	char buf[BUFSIZ];
	char *s;

	/* read the next non-blank non-comment line */
	do {
		if (fgets(buf, sizeof(buf), fd) == NULL)
			return feof(fd) ? 0 : -1;
		s = strchr(buf, '\n');
		if (!s) {
			/* Missing final newline?  Otherwise extremely */
			/* long line - assume file was corrupted */
			if (feof(fd))
				s = strchr(buf, '\0');
			else {
				fprintf(stderr, "config file: missing newline at line '%s'.",
					buf);
				return -1;
			}
		}
		*s = '\0';
		if (--s >= buf && *s == '\r')
			*s = '\0';

		s = buf;
		while (*s == ' ' || *s == '\t')		/* skip space */
			s++;

	} while (*s == '\0' || *s == '#');

	if (!strncmp(s, "INTERVAL=", 9)) {
		s += 9;
		if (*s) {
			if (str_toi(&conf->scan_interval, s, NULL, 10) != 0 ||
				conf->scan_interval < LEDMON_MIN_SLEEP_INTERVAL)
				conf->scan_interval = LEDMON_MIN_SLEEP_INTERVAL;
		}
	} else if (!strncmp(s, "LOG_LEVEL=", 10)) {
		s += 10;
		_set_log_level(conf, s);
	} else if (!strncmp(s, "LOG_PATH=", 9)) {
		s += 9;
		if (*s)
			set_log_path(conf, s);
	} else if (!strncmp(s, "BLINK_ON_MIGR=", 14)) {
		s += 14;
		conf->blink_on_migration = parse_bool(s);
		if (conf->blink_on_migration < 0)
			return -1;
	} else if (!strncmp(s, "BLINK_ON_INIT=", 14)) {
		s += 14;
		conf->blink_on_init = parse_bool(s);
		if (conf->blink_on_init < 0)
			return -1;
	} else if (!strncmp(s, "REBUILD_BLINK_ON_ALL=", 21)) {
		s += 21;
		conf->rebuild_blink_on_all = parse_bool(s);
		if (conf->rebuild_blink_on_all < 0)
			return -1;
	} else if (!strncmp(s, "RAID_MEMBERS_ONLY=", 18)) {
		s += 18;
		conf->raid_members_only = parse_bool(s);
		if (conf->raid_members_only < 0)
			return -1;
	} else if (!strncmp(s, "USERSPACE_NPEM=", 15)) {
		s += 15;
		conf->userspace_npem = parse_bool(s);
		if (conf->userspace_npem < 0)
			return -1;
	} else if (_parse_and_add_to_list(s, WHITELIST, WHITELIST_LEN, &conf->cntrls_allowlist)) {
		/* Deprecated, provided for backwards compatibility */
		return 0;
	} else if (_parse_and_add_to_list(s, BLACKLIST, BLACKLIST_LEN, &conf->cntrls_excludelist)) {
		/* Deprecated, provided for backwards compatibility */
		return 0;
	} else if (_parse_and_add_to_list(s, ALLOWLIST, ALLOWLIST_LEN, &conf->cntrls_allowlist)) {
		return 0;
	} else if (_parse_and_add_to_list(s, EXCLUDELIST,  EXCLUDELIST_LEN,
					  &conf->cntrls_excludelist)) {
		return 0;
	} else {
		fprintf(stderr, "config file: unknown option '%s'.\n", s);
		return -1;
	}
	return 0;
}

void ledmon_free_conf(struct ledmon_conf *conf)
{
	list_erase(&conf->cntrls_excludelist);
	list_erase(&conf->cntrls_allowlist);

	if (conf->log_path)
		free(conf->log_path);
}

/* return real config data or built-in default */
int ledmon_read_conf(const char *filename, struct ledmon_conf *conf)
{
	FILE *f;

	if (!filename || (filename && access(filename, F_OK) < 0)) {
		if (filename)
			fprintf(stderr, "%s: does not exist, using global config file\n",
				filename);
		filename = LEDMON_DEF_CONF_FILE;
	}

	f = fopen(filename, "re");
	if (!f) {
		fprintf(stderr, "%s: does not exist, using built-in defaults\n",
			filename);
	} else {
		while (!feof(f)) {
			if (parse_next(f, conf)) {
				fprintf(stderr, "%s: parse error\n", filename);
				ledmon_free_conf(conf);
				fclose(f);
				return STATUS_CONFIG_FILE_ERROR;
			}
		}
		fclose(f);
	}

	if (!list_is_empty(&conf->cntrls_allowlist) &&
	    !list_is_empty(&conf->cntrls_excludelist))
		fprintf(stderr,
			"Both ALLOWLIST and EXCLUDELIST are specified - ignoring EXCLUDELIST.\n");

	return STATUS_SUCCESS;
}

static char *conf_list_to_str(struct list *list)
{
	char buf[BUFSIZ];
	char *elem;

	memset(buf, 0, sizeof(buf));
	list_for_each(list, elem) {
		int curr = strlen(buf);
		snprintf(buf + curr, sizeof(buf) - curr, "%s,", elem);
	}

	return strdup(buf);
}

int ledmon_write_shared_conf(struct ledmon_conf *conf)
{
	char buf[BUFSIZ];
	char *allowlist = NULL;
	char *excludelist = NULL;
	void *shared_mem_ptr;
	int fd = shm_open(LEDMON_SHARE_MEM_FILE, O_RDWR | O_CREAT, 0644);

	if (fd == -1)
		return STATUS_FILE_OPEN_ERROR;

	if (ftruncate(fd, sizeof(buf)) != 0) {
		close(fd);
		return STATUS_FILE_WRITE_ERROR;
	}

	shared_mem_ptr = mmap(NULL, sizeof(buf), PROT_WRITE, MAP_SHARED, fd, 0);
	if (shared_mem_ptr == MAP_FAILED) {
		close(fd);
		return STATUS_FILE_WRITE_ERROR;
	}

	snprintf(buf, sizeof(buf),
		 "BLINK_ON_INIT=%d\n", conf->blink_on_init);
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		 "BLINK_ON_MIGR=%d\n", conf->blink_on_migration);
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		 "LOG_LEVEL=%u\n", conf->log_level);
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		 "LOG_PATH=%s\n", conf->log_path);
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		 "RAID_MEMBERS_ONLY=%d\n", conf->raid_members_only);
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		 "REBUILD_BLINK_ON_ALL=%d\n", conf->rebuild_blink_on_all);
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		 "INTERVAL=%d\n", conf->scan_interval);
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		 "USERSPACE_NPEM=%d\n", conf->userspace_npem);
	allowlist = conf_list_to_str(&conf->cntrls_allowlist);
	if (allowlist) {
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
			 "%s%s\n", ALLOWLIST, allowlist);
		free(allowlist);
	}
	excludelist = conf_list_to_str(&conf->cntrls_excludelist);
	if (excludelist) {
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
			 "%s%s\n", EXCLUDELIST, excludelist);
		free(excludelist);
	}

	memcpy(shared_mem_ptr, buf, strlen(buf));
	munmap(shared_mem_ptr, strlen(buf));
	close(fd);

	return STATUS_SUCCESS;
}

int ledmon_remove_shared_conf(void)
{
	return shm_unlink(LEDMON_SHARE_MEM_FILE);
}

