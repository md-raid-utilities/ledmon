// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2009 Intel Corporation.

/* System headers */
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

/* Public headers */
#include <led/libled.h>

/* Local headers */
#include "config.h"
#include "common/config_file.h"
#include "help.h"
#include "libled_internal.h"

#if _HAVE_DMALLOC_H
#include <dmalloc.h>
#endif

#ifdef ENABLE_TEST
#define COMMON_GETOPT_ARGS	\
	OPT_ALL,		\
	OPT_DEBUG,		\
	OPT_ERROR,		\
	OPT_INFO,		\
	OPT_QUIET,		\
	OPT_WARNING,		\
	OPT_LOG,		\
	OPT_LOG_LEVEL,		\
	OPT_HELP,		\
	OPT_TEST
#else
#define COMMON_GETOPT_ARGS	\
	OPT_ALL,		\
	OPT_DEBUG,		\
	OPT_ERROR,		\
	OPT_INFO,		\
	OPT_QUIET,		\
	OPT_WARNING,		\
	OPT_LOG,		\
	OPT_LOG_LEVEL,		\
	OPT_HELP
#endif

struct map ledctl_status_map[] = {
	{ "STATUS_SUCCESS", LED_STATUS_SUCCESS },
	{ "STATUS_IBPI_DETERMINE_ERROR", LED_STATUS_IBPI_DETERMINE_ERROR },
	{ "STATUS_INVALID_PATH", LED_STATUS_INVALID_PATH },
	{ "STATUS_INVALID_STATE", LED_STATUS_INVALID_STATE },
	{ "STATUS_LIST_EMPTY", LED_STATUS_LIST_EMPTY },
	{ "STATUS_ONEXIT_ERROR", LED_STATUS_ONEXIT_ERROR },
	{ "STATUS_NOT_SUPPORTED", LED_STATUS_NOT_SUPPORTED },
	{ "STATUS_STAT_ERROR", LED_STATUS_STAT_ERROR },
	{ "STATUS_CMDLINE_ERROR", LED_STATUS_CMDLINE_ERROR },
	{ "STATUS_NOT_A_PRIVILEGED_USER", LED_STATUS_NOT_A_PRIVILEGED_USER },
	{ "STATUS_LOG_FILE_ERROR", LED_STATUS_LOG_FILE_ERROR },
	{ NULL, LED_STATUS_UNDEFINED}
};

/**
 * @brief Get string status for ledctl.
 *
 * @param	s	ledctl status code.
 * @return	string status if defined, else "???".
 */
static char *ledctl_strstatus(led_status_t s)
{
	char *status_str = str_map(s, ledctl_status_map);

	return status_str ? status_str : "???";
}

/**
 * @brief An IBPI state structure.
 *
 * This structure connects an IBPI pattern and block devices. It is used by
 * _determine() function to figure the correct pattern out.
 */
struct ibpi_state {
	enum led_ibpi_pattern ibpi;
	struct list block_list;
};

/**
 * @brief List of IBPI patterns.
 *
 * This is a list of IBPI patterns the user requested to be visualized.
 * Each element on the list is struct ibpi_state type. There's only one
 * instance of each IBPI pattern on the list (no duplicates).
 */
static struct list ibpi_list;

enum print_param {
	PRINT_ALL,
	PRINT_STATE,
	PRINT_SLOT,
	PRINT_DEVICE
};

/**
 * @brief slot request parameters
 *
 * This structure contains all possible parameters for related commands.
 */
struct request {
	/**
	 * Option given in the request.
	 */
	int chosen_opt;

	/**
	 * Name of the device.
	 */
	char device[PATH_MAX];

	/**
	 * Unique slot identifier.
	 */
	char slot[PATH_MAX];

	/**
	 * Type of the controller.
	 */
	enum led_cntrl_type cntrl;

	/**
	 * IBPI state.
	 */
	enum led_ibpi_pattern state;

	/**
	 * Slot parameter to be printed on output.
	 */
	enum print_param to_print;
};

static struct led_ctx *ctx;

struct ledmon_conf conf;

static int possible_params_modes[] = {
	OPT_HELP,
	OPT_VERSION,
	OPT_GET_SLOT,
	OPT_SET_SLOT,
	OPT_LIST_SLOTS,
	OPT_LIST_CTRL,
	OPT_DEFAULT_CTRL,
	OPT_IBPI
};

static int possible_params_list_ctrl[] = {
	COMMON_GETOPT_ARGS
};

static int possible_params_default_ctrl[] = {
	OPT_DEVICE,
	COMMON_GETOPT_ARGS
};

static int possible_params_set_slot[] = {
	OPT_CNTRL_TYPE,
	OPT_DEVICE,
	OPT_SLOT,
	OPT_STATE,
	COMMON_GETOPT_ARGS
};

static int possible_params_get_slot[] = {
	OPT_CNTRL_TYPE,
	OPT_DEVICE,
	OPT_SLOT,
	OPT_PRINT_PARAM,
	COMMON_GETOPT_ARGS
};

static int possible_params_list_slots[] = {
	OPT_CNTRL_TYPE,
	COMMON_GETOPT_ARGS
};

static int possible_params_ibpi[] = {
	OPT_LISTED_ONLY,
	COMMON_GETOPT_ARGS
};

static int listed_only;

static bool test_params;

static void ibpi_state_fini(struct ibpi_state *p)
{
	list_clear(&p->block_list);
	free(p);
}

/**
 * @brief Finalizes LED control utility.
 *
 * This is internal function of ledctl utility. The function cleans up a memory
 * allocated for the application and closes all opened handles. This function is
 * design to be registered as atexit() handler function.
 *
 * @return The function does not return a value.
 */
static void _ledctl_fini(void)
{
	led_free(ctx);
	list_erase(&ibpi_list);
	log_close(&conf);
}

/**
 * @brief Puts new IBPI state on the list.
 *
 * This is internal function of ledctl utility. The function creates a new entry
 * of the list with IBPI patterns. Each IBPI state has a list of block devices
 * attached. The function initializes this list and sets empty.
 *
 * @param[in]      ibpi           an IBPI pattern to add.
 *
 * @return Pointer to the created element if successful, otherwise function
 *         returns NULL. The NULL pointer means element allocation failed.
 */
static struct ibpi_state *_ibpi_state_init(enum led_ibpi_pattern ibpi)
{
	struct ibpi_state *state = malloc(sizeof(struct ibpi_state));

	if (!state)
		return NULL;

	list_init(&state->block_list, NULL);
	state->ibpi = ibpi;

	if (!list_append(&ibpi_list, state)) {
		free(state);
		return NULL;
	}

	return state;
}

static struct ibpi_state *_ibpi_find(const struct list *ibpi_local_list,
				     enum led_ibpi_pattern ibpi)
{
	struct ibpi_state *state;

	list_for_each(ibpi_local_list, state) {
		if (state->ibpi == ibpi)
			return state;
	}
	return NULL;
}

/**
 * @brief Gets a pointer to IBPI state structure.
 *
 * This is internal function of ledctl utility. The function retrieves an entry
 * to an IBPI state structure from ibpi_list list. If such an entry does not
 * exist the memory is allocated and entry is added to the list.
 *
 * @param[in]      name       a name of IBPI pattern i.e. taken from command
 *                            line interface. It might be 'locate', 'normal',
 *                            'locate_off', 'off', 'ica', 'degraded', 'rebuild',
 *                            'ifa', 'failed_array', 'hotspare',
 *                            'pfa', 'failure' or 'disk_failed' string.
 *
 * @return Pointer to IBPI state structure if successful, otherwise the function
 *         returns NULL. The NULL pointer means either the invalid status name
 *         has been given or there's not enough memory available in the system
 *         to allocate the structure.
 */
static struct ibpi_state *_ibpi_state_get(const char *name)
{
	enum led_ibpi_pattern ibpi = string2ibpi(name);
	struct ibpi_state *state = NULL;

	if (ibpi == LED_IBPI_PATTERN_UNKNOWN)
		return NULL;

	state = _ibpi_find(&ibpi_list, ibpi);
	if (state == NULL)
		state = _ibpi_state_init(ibpi);
	return state;
}

bool _block_device_search(const struct list *block_list,
						 const char *path)
{
	char *block;

	list_for_each(block_list, block) {
		if (strcmp(block, path) == 0)
			return true;
	}
	return false;
}

/**
 * @brief Adds a block device to a block list.
 *
 * This is internal function of ledctl utility. Each IBPI state has list of
 * block devices attached to. The function puts a pointer to a block device
 * on that list. First the function determines the canonical version of the
 * given path and checks if it is correct. If the path to /dev directory is
 * given the function finds out the correct entry in sysfs tree.
 *
 * @param[in]      state          pointer to IBPI state structure the block
 *                                device will be added to.
 * @param[in]      name           path to block device.
 *
 * @return The function does not return a value.
 */
static led_status_t _ibpi_state_add_block(struct ibpi_state *state, char *name)
{
	char path[PATH_MAX];
	led_status_t rc = led_device_name_lookup(ctx, name, path);

	if (rc != LED_STATUS_SUCCESS) {
		log_error("Could not find %s.", name);
		return rc;
	}

	if (!led_is_management_supported(ctx, path)) {
		log_error("%s: device not supported", name);
		return LED_STATUS_NOT_SUPPORTED;
	}

	if (!_block_device_search(&state->block_list, path)) {
		char *path_copy = strdup(path);

		if (!path_copy || !list_append(&state->block_list, path_copy)) {
			free(path_copy);
			return LED_STATUS_OUT_OF_MEMORY;
		}
	} else {
		log_info("%s: %s: device already on the list.", ibpi2str(state->ibpi), path);
	}
	return LED_STATUS_SUCCESS;
}

static led_status_t verify_block_lists(void)
{
	if (!list_is_empty(&ibpi_list)) {
		struct ibpi_state *state;

		list_for_each(&ibpi_list, state)
			if (list_is_empty(&state->block_list))
				log_warning("IBPI %s: missing block device(s)... pattern ignored.",
					    ibpi2str(state->ibpi));
	} else {
		log_error("missing operand(s)... run %s --help for details.", progname);
		return LED_STATUS_LIST_EMPTY;
	}
	return LED_STATUS_SUCCESS;
}

/**
 * @brief Command line parser - operands.
 *
 * This is internal function of ledctl utility. The function parses operands of
 * ledctl application. The operands section contains the pattern name and a list
 * of block devices associated with each pattern. There are two different
 * formats for the operand. First format is pattern={ dev_list }, where elements
 * are space separated on the dev_list. Second format is pattern=dev1,dev2,...
 * where elements are comma separated on the list of devices.
 *
 * @param[in]      argc           number of elements in argv array.
 * @param[in]      argv           command line arguments.
 *
 * @return LED_STATUS_SUCCESS if successful, otherwise led_status_t.
 */
static led_status_t _cmdline_ibpi_parse(int argc, char *argv[])
{
	led_status_t t_status, ret_status = LED_STATUS_SUCCESS;

	while (optind < argc) {
		struct ibpi_state *state = NULL;
		char *p = argv[optind++];
		char *t;
		t = strchrnul(p, '=');
		if (*t != '\0') {
			*(t++) = '\0';
			state = _ibpi_state_get(p);
			if (state == NULL) {
				log_error("%s - unknown pattern name.", p);
				return LED_STATUS_INVALID_STATE;
			}
			if (*t == '{') {
				while ((t = argv[optind++]) != NULL) {
					if (*t == '}')
						break;
					t_status =
					_ibpi_state_add_block(state, t);
					if (t_status != LED_STATUS_SUCCESS)
						ret_status = t_status;
				}
			} else {
				while (*(p = t) != '\0') {
					t = strchrnul(p, ',');
					if (*t != '\0')
						*(t++) = '\0';
					t_status =
					_ibpi_state_add_block(state, p);
					if (t_status != LED_STATUS_SUCCESS)
						ret_status = t_status;
				}
			}
		}
	}

	if (verify_block_lists() != LED_STATUS_SUCCESS)
		ret_status = LED_STATUS_IBPI_DETERMINE_ERROR;

	return ret_status;
}

/**
 * @brief Inits request structure with initial values.
 *
 * @param[in]       req       structure with request
 *
 * @return This function does not return a value.
 */
static void request_init(struct request *req)
{
	memset(req, 0, sizeof(struct request));

	req->chosen_opt = OPT_NULL_ELEMENT;
	req->state = LED_IBPI_PATTERN_UNKNOWN;
}

/**
 * @brief Gets parameter to be printed on output.
 *
 * @param[in]       to_print     parameter based on user input.
 *
 * @return Parameter name or PRINT_ALL if all will be printed.
 */
static enum print_param get_param_to_print(const char *to_print)
{
	if (strcasecmp(to_print, "state") == 0)
		return PRINT_STATE;
	else if (strcasecmp(to_print, "slot") == 0)
		return PRINT_SLOT;
	else if (strcasecmp(to_print, "device") == 0)
		return PRINT_DEVICE;
	else
		return PRINT_ALL;
}

/**
 * @brief Displays the help.
 *
 * This is internal function of ledctl utility.
 * The function prints the name and version of the program out.
 * It displays the usage and available options and its arguments (if any).
 * Each option is described.
 */
void _cmdline_parse_mode_help(int argc, char *argv[], enum opt mode)
{
	static int params[] = {OPT_HELP};
	int optind_backup = optind;
	int status = EXIT_SUCCESS;
	struct option *longopts;
	char *shortopts;
	int opt;
	int opt_index;

	setup_options(&longopts, &shortopts, params, ARRAY_SIZE(params));

	opt = getopt_long(argc, argv, shortopts, longopts, &opt_index);
	if (opt != 'h') {
		/*
		 * Restore option index to enable correct
		 * processing of arguments in further stages.
		 */
		optind = optind_backup;
		return;
	}

	print_mode_help(mode);

	free(longopts);
	free(shortopts);
	exit(status);
}

/**
 * @brief Command line parser - modes.
 *
 * This is internal function of ledctl utility. The function parses modes
 * of ledctl application. Refer to ledctl help in order to get more information
 * about ledctl command line options.
 *
 * @param[in]      argc           program arguments count.
 * @param[in]      argv           program arguments.
 * @param[in]      req            structure with request.
 *
 * @return This function does not return a value.
 */
void _cmdline_parse_modes(int argc, char *argv[], struct request *req)
{
	char *shortopts;
	struct option *longopts;
	int opt_index;
	int opt;

	setup_options(&longopts, &shortopts, possible_params_modes,
		      ARRAY_SIZE(possible_params_modes));

	/* Check first parameter only */
	opt = getopt_long(argc, argv, shortopts, longopts, &opt_index);
	switch (opt) {
	case 'v':
		req->chosen_opt = OPT_VERSION;
		break;
	case 'h':
		req->chosen_opt = OPT_HELP;
		break;
	case 'G':
		req->chosen_opt = OPT_GET_SLOT;
		break;
	case 'P':
		req->chosen_opt = OPT_LIST_SLOTS;
		break;
	case 'S':
		req->chosen_opt = OPT_SET_SLOT;
		break;
	case 'L':
		req->chosen_opt = OPT_LIST_CTRL;
		break;
	case 'B':
		req->chosen_opt = OPT_DEFAULT_CTRL;
		break;
	case 'I':
		req->chosen_opt = OPT_IBPI;
		break;
	default:
		/* It is fair to assume IBPI here, we need to reset option index */
		optind = 1;
		req->chosen_opt = OPT_IBPI;
	}
}

/**
 * @brief Setup mode options.
 *
 * The function setups possible options depending on detected mode.
 *
 * @param[in]      req            structure with request.
 * @param[in]      shortopts      legitimate option characters.
 * @param[in]      longopts       pointer to the first element of an array of struct option.
 *
 * @return bool if successful, otherwise false.
 */
bool _setup_mode_options(struct request * const req, char **shortopts, struct option **longopts)
{
	switch (req->chosen_opt) {
	case OPT_GET_SLOT:
		setup_options(longopts, shortopts, possible_params_get_slot,
			      ARRAY_SIZE(possible_params_get_slot));
		break;
	case OPT_LIST_SLOTS:
		setup_options(longopts, shortopts, possible_params_list_slots,
			      ARRAY_SIZE(possible_params_list_slots));
		break;
	case OPT_SET_SLOT:
		setup_options(longopts, shortopts, possible_params_set_slot,
			      ARRAY_SIZE(possible_params_set_slot));
		break;
	case OPT_LIST_CTRL:
		setup_options(longopts, shortopts, possible_params_list_ctrl,
			      ARRAY_SIZE(possible_params_list_ctrl));
		break;
	case OPT_DEFAULT_CTRL:
		setup_options(longopts, shortopts, possible_params_default_ctrl,
			      ARRAY_SIZE(possible_params_default_ctrl));
		break;
	case OPT_IBPI:
		setup_options(longopts, shortopts, possible_params_ibpi,
			      ARRAY_SIZE(possible_params_ibpi));
		break;
	case OPT_VERSION:
	case OPT_HELP:
	default:
		log_error("Detected non supported request option.");
		return false;
	}

	return true;
}

/**
 * @brief Command line parser - non modes options.
 *
 * This is internal function of ledctl utility. The function parses non modes
 * parameters of ledctl application. Refer to ledctl help in order to get more
 * information about ledctl command line options.
 *
 * @param[in]      opt            option character to be parsed.
 * @param[in]      opt_index      index of the long option relative to longopts.
 * @param[in]      longopts       pointer to the first element of an array of struct option.
 * @param[in]      req            structure with request.
 *
 * @return LED_STATUS_SUCCESS if successful, otherwise one of failure led_status_t.
 */
led_status_t _cmdline_parse_params(int opt, int opt_index, struct option *longopts,
				   struct request *req)
{
	switch (opt) {

	case 0:
	{
		int option_id = get_option_id(longopts[opt_index].name);

		switch (option_id) {
		case OPT_LOG_LEVEL:
		{
			int log_level = get_option_id(optarg);

			if (log_level != -1)
				set_verbose_level(&conf, log_level);
			else
				return LED_STATUS_CMDLINE_ERROR;
			break;
		}
		default:
			set_verbose_level(&conf, option_id);
		}
		break;
	}
	case 'l':
		set_log_path(&conf, optarg);
		break;
	case 'x':
		listed_only = 1;
		break;
#ifdef ENABLE_TEST
	case 'T':
		test_params = true;
		break;
#endif
	case 'n':
		req->cntrl = led_string_to_cntrl_type(optarg);
		break;
	case 's':
	{
		struct ibpi_state *state = _ibpi_state_get(optarg);

		if (state) {
			req->state = state->ibpi;
		} else {
			log_error("Invalid IBPI state: '%s'.", optarg);
			return LED_STATUS_CMDLINE_ERROR;
		}
		free(state);
		break;
	}
	case 'd':
		strncpy(req->device, optarg, PATH_MAX - 1);
		break;
	case 'p':
		strncpy(req->slot, optarg, PATH_MAX - 1);
		break;
	case 'r':
		req->to_print = get_param_to_print(optarg);
		break;
	case 'h':
		_print_incorrect_help_usage();
		return LED_STATUS_NOT_SUPPORTED;
	case ':':
	case '?':
	default:
		return LED_STATUS_CMDLINE_ERROR;
	}

	return LED_STATUS_SUCCESS;
}

/**
 * @brief Command line parser - options.
 *
 * This is internal function of ledctl utility. The function parses options of
 * ledctl application. Refer to ledctl help in order to get more information
 * about ledctl command line options.
 *
 * @param[in]      argc           number of elements in argv array.
 * @param[in]      argv           command line arguments.
 * @param[in]      req            structure with request.
 *
 * @return LED_STATUS_SUCCESS if successful, otherwise led_status_t.
 */
led_status_t _cmdline_parse(int argc, char *argv[], struct request *req)
{
	int opt, opt_index = -1;
	char *shortopts;
	struct option *longopts;
	led_status_t ret = LED_STATUS_SUCCESS;

	if (!_setup_mode_options(req, &shortopts, &longopts))
		return LED_STATUS_CMDLINE_ERROR;

	do {
		opt = getopt_long(argc, argv, shortopts, longopts, &opt_index);

		if (opt == -1)
			break;
		ret = _cmdline_parse_params(opt, opt_index, longopts, req);
		if (!ret)
			continue;
		if (ret == LED_STATUS_CMDLINE_ERROR)
			log_error("Cannot parse parameter '%s'. It may be invalid or not supported for selected mode.",
				   argv[optind - 1]);
		break;
	} while (1);
	free(longopts);
	free(shortopts);
	return ret;
}

void execute_non_root_request(struct request *req)
{
	switch (req->chosen_opt) {
	case OPT_VERSION:
		_ledctl_version();
		exit(EXIT_SUCCESS);
	case OPT_HELP:
		_print_main_help();
		exit(EXIT_SUCCESS);
	}
}

struct led_slot_list_entry *find_slot(struct led_ctx *ctx, struct request *req)
{
	if (req->device[0] != '\0')
		return led_slot_find_by_device_name(ctx, req->cntrl, req->device);
	else if (req->slot[0] != '\0')
		return led_slot_find_by_slot(ctx, req->cntrl, req->slot);
	return NULL;
}

/**
 * @brief Verifies request parameters.
 *
 * @param[in]       ctx       library context.
 * @param[in]       req       structure with request.
 *
 * @return LED_STATUS_SUCCESS if successful, otherwise led_status_t.
 */
static led_status_t verify_request(struct led_ctx *ctx, struct request *req)
{
	if (req->chosen_opt == OPT_LIST_CTRL)
		return LED_STATUS_SUCCESS;
	if (req->chosen_opt == OPT_DEFAULT_CTRL) {
		if (!req->device[0]) {
			log_error("Device is missing, aborting.");
			return LED_STATUS_CMDLINE_ERROR;
		} else
			return LED_STATUS_SUCCESS;
	}
	if (req->cntrl == LED_CNTRL_TYPE_UNKNOWN) {
		log_error("Invalid controller in the request.");
		return LED_STATUS_INVALID_CONTROLLER;
	}
	if (req->chosen_opt == OPT_SET_SLOT && req->state == LED_IBPI_PATTERN_UNKNOWN) {
		log_error("Invalid IBPI state in the request.");
		return LED_STATUS_INVALID_STATE;
	}
	if (req->device[0] && req->slot[0]) {
		log_error("Device and slot parameters are exclusive.");
		return LED_STATUS_DATA_ERROR;
	}
	if (req->chosen_opt != OPT_LIST_SLOTS) {
		struct led_slot_list_entry *slot = find_slot(ctx, req);

		if (!slot) {
			log_error("Slot was not found for provided parameters.");
			return LED_STATUS_CMDLINE_ERROR;
		}
		led_slot_list_entry_free(slot);
	}
	return LED_STATUS_SUCCESS;
}

static void print_slot(struct led_slot_list_entry *s, enum print_param to_print)
{
	const char *device_name = led_slot_device(s);
	const char *slot_id = basename(led_slot_id(s));
	const char *ibpi_str = ibpi2str(led_slot_state(s));

	if (!device_name)
		device_name = "(empty)";

	switch (to_print) {
	case PRINT_SLOT:
		printf("%s\n", slot_id);
		return;
	case PRINT_DEVICE:
		printf("%s\n", device_name);
		return;
	case PRINT_STATE:
		printf("%s\n", ibpi_str);
		return;
	default:
		printf("slot: %-15s led state: %-15s device: %-15s\n",
			slot_id, ibpi_str, device_name);
	}
}

static void print_cntrl(struct led_cntrl_list_entry *cntrl)
{
	printf("%s (%s)\n", led_cntrl_path(cntrl),
		led_cntrl_type_to_string(led_cntrl_type(cntrl)));
}

/**
 * @brief List slots connected to given controller
 *
 * This function scans all available slots connected to given controller
 * and prints their led states and names of the connected devices (if exist).
 *
 * @param[in]       cntrl_type       What type of controller to list slots for
 *
 * @return LED_STATUS_SUCCESS if successful, otherwise led_status_t.
 */

static led_status_t list_slots(enum led_cntrl_type cntrl_type)
{
	struct led_slot_list_entry *slot = NULL;
	led_status_t  status = LED_STATUS_SUCCESS;
	struct led_slot_list *slot_list = NULL;

	status = led_slots_get(ctx, &slot_list);
	if (status != LED_STATUS_SUCCESS) {
		log_error("Failed to retrieve slots, reason %s\n",
				ledctl_strstatus((led_status_t)status));
		return status;
	}

	while ((slot = led_slot_next(slot_list))) {
		if (cntrl_type == led_slot_cntrl(slot))
			print_slot(slot, PRINT_ALL);
	}

	led_slot_list_free(slot_list);
	return status;
}

/**
 * @brief Executes proper slot mode function.
 *
 * @param[in]       ctx       library context.
 * @param[in]       req       structure with request.
 *
 * @return LED_STATUS_SUCCESS if successful, otherwise led_status_t.
 */
led_status_t execute_request(struct led_ctx *ctx, struct request *req)
{
	struct led_slot_list_entry *slot;

	if (req->chosen_opt == OPT_LIST_SLOTS)
		return list_slots(req->cntrl);
	if (req->chosen_opt == OPT_DEFAULT_CTRL) {
		enum led_cntrl_type cntrl_type;
		char device_path[PATH_MAX];

		led_device_name_lookup(ctx, req->device, device_path);
		cntrl_type = led_is_management_supported(ctx, device_path);

		if (cntrl_type == LED_CNTRL_TYPE_UNKNOWN)
			lib_log(ctx, LED_LOG_LEVEL_ERROR,
				"Unable to determine controller for device\n");
		else
			printf("%s\n", led_cntrl_type_to_string(cntrl_type));

		return LED_STATUS_SUCCESS;
	}

	if (req->chosen_opt == OPT_LIST_CTRL) {
		struct led_cntrl_list_entry *cntrl = NULL;
		struct led_cntrl_list *cntrls = NULL;
		led_status_t status;

		status = led_cntrls_get(ctx, &cntrls);
		if (status != LED_STATUS_SUCCESS) {
			log_error("Error on controller retrieval %s\n",
					ledctl_strstatus((led_status_t)status));
			exit(EXIT_FAILURE);
		}

		while ((cntrl = led_cntrl_next(cntrls)))
			print_cntrl(cntrl);

		led_cntrl_list_free(cntrls);

		exit(EXIT_SUCCESS);
	}

	slot = find_slot(ctx, req);
	if (slot == NULL)
		return LED_STATUS_DATA_ERROR;

	switch (req->chosen_opt) {
	case OPT_SET_SLOT:
	{
		led_status_t set_rc = LED_STATUS_SUCCESS;

		if (req->state != LED_IBPI_PATTERN_LOCATE_OFF
		    && led_slot_state(slot) == req->state) {
			log_warning("Led state: %s is already set for the slot.",
				    ibpi2str(req->state));
		} else {
			set_rc = led_slot_set(ctx, slot, req->state);
		}
		led_slot_list_entry_free(slot);
		return set_rc;
	}
	case OPT_GET_SLOT:
		print_slot(slot, req->to_print);
		led_slot_list_entry_free(slot);
		return LED_STATUS_SUCCESS;
	default:
		led_slot_list_entry_free(slot);
		return LED_STATUS_NOT_SUPPORTED;
	}
}

/**
 * @brief Send IBPI pattern.
 *
 * This is internal function of ledctl utility. The function set a requested
 * ibpi_state for devices linked with this ibpi_state on ibpi_local_list.
 * For other devices LED_IBPI_PATTERN_LOCATE_OFF might be set - depending on
 * listed_only parameter. Then it sends a LED control message to controller
 * to visualize the pattern.
 *
 * @param[in]      ibpi_local_list	    pointer to list of ipbi_state.
 *
 * @return LED_STATUS_SUCCESS if successful, otherwise LED_STATUS_IBPI_DETERMINE_ERROR
 */
static led_status_t _ledctl_execute_ibpi(struct list *ibpi_local_list)
{
	struct ibpi_state *state;
	char *device;

	if (!listed_only) {
		off_all(ctx);
	}

	list_for_each(ibpi_local_list, state)
		list_for_each(&state->block_list, device) {
			led_set(ctx, device, state->ibpi);
		}

	led_flush(ctx);

	return LED_STATUS_SUCCESS;
}

static led_status_t _read_shared_conf(void)
{
	led_status_t status;
	char share_conf_path[PATH_MAX];

	memset(share_conf_path, 0, sizeof(share_conf_path));
	snprintf(share_conf_path, sizeof(share_conf_path), "/dev/shm%s",
		 LEDMON_SHARE_MEM_FILE);

	status = ledmon_read_conf(share_conf_path, &conf);
	return status;
}

/**
 * @brief Unset unsupported config parameters.
 *
 * For ledctl only LOG_LEVEL and LOG_PATH are supported and desired.
 * Unset other options.
 */
static void _unset_unused_options(void)
{
	conf.blink_on_init = false;
	conf.blink_on_migration = false;
	list_erase(&conf.cntrls_excludelist);
	list_erase(&conf.cntrls_allowlist);
	conf.raid_members_only = false;
	conf.rebuild_blink_on_all = false;
	conf.scan_interval = 0;
}

static led_status_t _init_ledctl_conf(void)
{
	return ledmon_init_conf(&conf, LED_LOG_LEVEL_WARNING, LEDCTL_DEF_LOG_FILE);
}

static const char *get_log_level_name(enum led_log_level_enum log_level)
{
	switch (log_level) {
	case LED_LOG_LEVEL_UNDEF:
		return "UNDEF";
	case LED_LOG_LEVEL_QUIET:
		return "QUIET";
	case LED_LOG_LEVEL_ERROR:
		return "ERROR";
	case LED_LOG_LEVEL_WARNING:
		return "WARNING";
	case LED_LOG_LEVEL_INFO:
		return "INFO";
	case LED_LOG_LEVEL_DEBUG:
		return "DEBUG";
	case LED_LOG_LEVEL_ALL:
		return "ALL";
	default:
		return "UNDEF";
	}
}

static void print_configuration(void)
{
	printf("LOG_LEVEL=%s\n", get_log_level_name(conf.log_level));
	printf("LOG_PATH=%s\n", conf.log_path);
}

/**
 * @brief Propagate the configuration setting to the library settings
 */
static led_status_t load_library_prefs(void)
{
	device_blink_behavior_set(ctx,
		conf.blink_on_migration,
		conf.blink_on_init,
		conf.rebuild_blink_on_all,
		conf.raid_members_only);

	led_log_fd_set(ctx, get_log_fd(&conf));
	led_log_level_set(ctx, conf.log_level);
	return LED_STATUS_SUCCESS;
}

/**
 * @brief Application's entry point.
 *
 * This is the entry point of ledctl utility. This function does all the work.
 * It allocates and initializes all used structures. Registers atexit()
 * handlers.
 * Then the function parses command line options and commands given and scans
 * sysfs tree for controllers, block devices and RAID devices. If no error is
 * the function send LED control messages according to IBPI pattern set.
 *
 * @param[in]      argc           number of elements in argv array, number of
 *                                command line arguments.
 * @param[in]      argv           array of command line arguments. The last
 *                                element on the list is NULL pointer.
 *
 * @return LED_STATUS_SUCCESS if successful, otherwise led_status_t.
 */
int main(int argc, char *argv[])
{
	led_status_t lib_rc;

	led_status_t status;
	struct request req;

	if (argc == 1) {
		fprintf(stderr, "Program cannot be run without parameters.\n");
		return LED_STATUS_CMDLINE_ERROR;
	}

	set_invocation_name(argv[0]);

	request_init(&req);

	/* Silence error if something is not recognized, errors are redefined */
	opterr = 0;

	_cmdline_parse_modes(argc, argv, &req);

	if ((req.chosen_opt == OPT_HELP || req.chosen_opt == OPT_VERSION))
		execute_non_root_request(&req);

	_cmdline_parse_mode_help(argc, argv, req.chosen_opt);

	if (req.chosen_opt == OPT_VERSION && argc > 2) {
		fprintf(stderr, "Parameter '%s' can be used alone only.\n",
			longopt_all[req.chosen_opt].name);
		exit(LED_STATUS_CMDLINE_ERROR);
	}

	if (geteuid() != 0) {
		fprintf(stderr, "Only root can run this application.\n");
		return LED_STATUS_NOT_A_PRIVILEGED_USER;
	}

	openlog(progname, LOG_PERROR, LOG_USER);

	lib_rc = led_new(&ctx);
	if (lib_rc != LED_STATUS_SUCCESS) {
		fprintf(stderr, "Unable to initialize LED library.\n");
		return lib_rc;
	}

	status = _init_ledctl_conf();
	if (status != LED_STATUS_SUCCESS)
		return status;

	if (atexit(_ledctl_fini))
		exit(LED_STATUS_ONEXIT_ERROR);

	status = _read_shared_conf();
	if (status != LED_STATUS_SUCCESS)
		return status;
	_unset_unused_options();

	status = _cmdline_parse(argc, argv, &req);
	if (status != LED_STATUS_SUCCESS || test_params) {
		if (test_params)
			print_configuration();
		exit(status);
	}

	status = log_open(&conf);
	if (status != LED_STATUS_SUCCESS)
		return LED_STATUS_LOG_FILE_ERROR;

	status = load_library_prefs();
	if (status != LED_STATUS_SUCCESS) {
		log_error("Unable to set library preferences %s", ledctl_strstatus(status));
		return status;
	}
	status = led_scan(ctx);
	if (status != LED_STATUS_SUCCESS) {
		log_error("Error on led_scan %s", ledctl_strstatus(status));
		return status;
	}

	if (status != LED_STATUS_SUCCESS)
		exit(LED_STATUS_CMDLINE_ERROR);

	list_init(&ibpi_list, (item_free_t)ibpi_state_fini);
	if (req.chosen_opt != OPT_NULL_ELEMENT && req.chosen_opt != OPT_IBPI) {
		status = verify_request(ctx, &req);
		if (status == LED_STATUS_SUCCESS)
			return execute_request(ctx, &req);

		exit(status);
	}
	status = _cmdline_ibpi_parse(argc, argv);
	if (status != LED_STATUS_SUCCESS) {
		log_debug("main(): _ibpi_parse() failed (status=%s).",
			  ledctl_strstatus(status));
		exit(status);
	}
	return _ledctl_execute_ibpi(&ibpi_list);
}
