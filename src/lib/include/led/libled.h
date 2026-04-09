// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022-2023 Red Hat, Inc.

/* LED library public interface */

#ifndef _LIB_LED_INCLUDED_
#define _LIB_LED_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

#include <linux/limits.h>
#include <stdbool.h>

#define LED_SYM_PUBLIC __attribute__((visibility("default")))


struct led_ctx;

enum led_log_level_enum {
	LED_LOG_LEVEL_UNDEF = 0,
	LED_LOG_LEVEL_QUIET = 1,
	LED_LOG_LEVEL_ERROR = 2,
	LED_LOG_LEVEL_WARNING = 3,
	LED_LOG_LEVEL_INFO = 4,
	LED_LOG_LEVEL_DEBUG = 5,
	LED_LOG_LEVEL_ALL = 6,
};

enum led_cntrl_type {
	LED_CNTRL_TYPE_UNKNOWN = 0,
	LED_CNTRL_TYPE_DELLSSD = 1,
	LED_CNTRL_TYPE_VMD = 2,
	LED_CNTRL_TYPE_SCSI = 3,
	LED_CNTRL_TYPE_AHCI = 4,
	LED_CNTRL_TYPE_NPEM = 5,
	LED_CNTRL_TYPE_AMD = 6,
	LED_CNTRL_TYPE_KERNEL_NPEM = 7,
};

/**
 * @brief Enumerated return values from library.
 */
typedef enum  {
	LED_STATUS_SUCCESS = 0,
	LED_STATUS_NULL_POINTER = 2,
	LED_STATUS_OUT_OF_MEMORY = 3,
	LED_STATUS_DATA_ERROR = 6,
	LED_STATUS_IBPI_DETERMINE_ERROR = 7,
	LED_STATUS_INVALID_PATH = 8,
	LED_STATUS_INVALID_STATE = 10,
	LED_STATUS_LIST_EMPTY = 21,
	LED_STATUS_ONEXIT_ERROR = 31,
	LED_STATUS_INVALID_CONTROLLER = 32,
	LED_STATUS_NOT_SUPPORTED = 33,
	LED_STATUS_STAT_ERROR = 34,
	LED_STATUS_CMDLINE_ERROR = 35,
	LED_STATUS_NOT_A_PRIVILEGED_USER = 36,
	LED_STATUS_LOG_FILE_ERROR = 40,
	LED_STATUS_UNDEFINED = 41
} led_status_t;

/**
 * @brief The following functions are for functionality pertaining to hardware controllers.
 */

/**
 * @brief Translates a controller string ID to enumerated type
 *
 * @param[in]	cntrl_str	String representation of controller type
 * @return enum led_cntrl_type
 */
enum led_cntrl_type LED_SYM_PUBLIC led_string_to_cntrl_type(const char *cntrl_str);

/**
 * @brief Translates the enumerated controller type to string representation
 *
 * @param[in]	cntrl		Enumerated constant
 * @return string representation of controller
 *
 * Note: Do not free string, do not try to modify string, it is a library constant
 */
const LED_SYM_PUBLIC char *const led_cntrl_type_to_string(enum led_cntrl_type cntrl);

/**
 * @brief Opaque data type for controller list entry
 */
struct led_cntrl_list_entry;

/**
 * @brief Opaque data type for controller list
 */
struct led_cntrl_list;

/**
 * @brief Retrieve the led_cntrl_list
 *
 * @param[in]	ctx	Library context
 * @param[out]	cntrls	Opaque list of controllers
 * @return led_status_t LED_STATUS_SUCCESS on success, else error reason.
 */
led_status_t LED_SYM_PUBLIC led_cntrls_get(struct led_ctx *ctx, struct led_cntrl_list **cntrls);

/**
 * @brief Frees the opaque list of controllers
 *
 * @param[in] cntrls	Opaque list of controllers to free
 */
void LED_SYM_PUBLIC led_cntrl_list_free(struct led_cntrl_list *cntrls);


/**
 * @brief Resets the iterator to beginning,
 *        follow with led_cntrl_next or led_cntrl_previous
 *
 * @param[in] cntrls	Opaque list of controllers
 */
void LED_SYM_PUBLIC led_cntrl_list_reset(struct led_cntrl_list *cntrls);

/**
 * @brief Iterator to retrieve next controller list entry from controller list
 *
 * @param[in] cntrls	Opaque data type for controller list
 * @return struct led_cntrl_list_entry or NULL if no more are available
 */
LED_SYM_PUBLIC struct led_cntrl_list_entry *led_cntrl_next(struct led_cntrl_list *cntrls);

/**
 * @brief Iterator to retrieve previous controller list entry from controller list
 *
 * @param[in] cntrls	Opaque data type for controller list
 * @return struct led_cntrl_list_entry or NULL if no more are available
 */
LED_SYM_PUBLIC struct led_cntrl_list_entry *led_cntrl_prev(struct led_cntrl_list *cntrls);

/**
 * @brief Retrieve the controller path for controller list entry
 *
 * @param[in] c		controller list entry
 * @return Controller path
 *
 * Note: This pointer has a lifetime of ctx or until reset is called. Copy value if you need
 *       longer lifetime.
 */
const LED_SYM_PUBLIC char *led_cntrl_path(struct led_cntrl_list_entry *c);

/**
 * @brief Retrieve the controller type for controller list entry
 *
 * @param[in] c		controller list entry
 * @return enum led_cntrl_type
 */
enum led_cntrl_type LED_SYM_PUBLIC  led_cntrl_type(struct led_cntrl_list_entry *c);

/**
 * @brief IBPI pattern identifies.
 *
 * The IBPI specification lists the following pattern names:
 *
 * - NORMAL      - either drive is present or missing, activity LED does not
 *                 matter. The rest of the LEDs are off.
 * - FAIL        - a block device has failed or is missing. Failure LED is
 *                 active and the behavior is depended on implementation
 *                 of enclosure management processor.
 * - REBUILD     - this means a RAID device is recovering or rebuilding
 *                 its data. Depending on implementation of enclosure
 *                 management processor appropriate LED is blinking or solid.
 * - ICA         - In a Critical Array, this means a RAID device is degraded and
 *                 there's no spare device available.
 * - IFA         - In a Failed Array, this means a RAID device is damaged and
 *                 cannot be recovered or rebuild.
 * - PFA         - Predict Failure Analysis state means that a block device will
 *                 fail soon, so it must be replaced with working one.
 * - LOCATE      - turns Locate LED on to identify a block device or slot.
 *
 * Additionally the following patterns has been introduced, just for the purpose
 * of LED control utility.
 *
 * - UNKNOWN     - unknown IBPI pattern and it means do not control LEDs for
 *                 a device it is set (no LED management).
 * - ONESHOT_NORMAL - this state means that ledmon just started and it does not
 *                 know anything about existing patterns set, so it will off all
 *                 the LEDs just in case of any problem in the future. The state
 *                 is set, when a RAID device disappears, too. Oneshot means
 *                 as soon application applies the state it will change
 *                 to UNKNOWN.
 * - ADDED         this state means that device previously known to ledmon is
 *                 restored. This state will be changed to ONESHOT_NORMAL.
 * - REMOVED       this state means that device was removed from system. It
 *                 will be changed to ADDED after restoring device to system.
 */
enum led_ibpi_pattern {
	LED_IBPI_PATTERN_UNKNOWN = 0,
	LED_IBPI_PATTERN_NONE = 1,	/* used only to initialize ibpi_prev */
	LED_IBPI_PATTERN_NORMAL = 2,
	LED_IBPI_PATTERN_ONESHOT_NORMAL = 3,
	LED_IBPI_PATTERN_DEGRADED = 4,
	LED_IBPI_PATTERN_HOTSPARE = 5,
	LED_IBPI_PATTERN_REBUILD = 6,
	LED_IBPI_PATTERN_FAILED_ARRAY = 7,
	LED_IBPI_PATTERN_PFA = 8,
	LED_IBPI_PATTERN_FAILED_DRIVE = 9,
	LED_IBPI_PATTERN_LOCATE = 10,
	LED_IBPI_PATTERN_LOCATE_OFF = 11,
	LED_IBPI_PATTERN_ADDED = 12,
	LED_IBPI_PATTERN_REMOVED = 13,
	LED_IBPI_PATTERN_LOCATE_AND_FAIL = 14,
	/*
	 * Below are SES-2 codes. Note that by default most IBPI messages are
	 * translated into SES when needed but SES codes can be added also.
	 */
	LED_SES_REQ_ABORT = 20,
	LED_SES_REQ_REBUILD = 21,
	LED_SES_REQ_IFA = 22,
	LED_SES_REQ_ICA = 23,
	LED_SES_REQ_CONS_CHECK = 24,
	LED_SES_REQ_HOTSPARE = 25,
	LED_SES_REQ_RSVD_DEV = 26,
	LED_SES_REQ_OK = 27,
	LED_SES_REQ_IDENT = 28,
	LED_SES_REQ_RM = 29,
	LED_SES_REQ_INS = 30,
	LED_SES_REQ_MISSING = 31,
	LED_SES_REQ_DNR = 32,
	LED_SES_REQ_ACTIVE = 33,
	LED_SES_REQ_EN_BB = 34,
	LED_SES_REQ_EN_BA = 35,
	LED_SES_REQ_DEV_OFF = 36,
	LED_SES_REQ_FAULT = 37,
	LED_SES_REQ_PRDFAIL = 38,
	LED_SES_REQ_IDENT_AND_FAULT = 39,
	LED_IBPI_PATTERN_COUNT = 50,
};

/* Backward compatibility, it was exposed in library API */
#define LED_IBPI_PATTERN_LOCATE_AND_FAILED_DRIVE LED_IBPI_PATTERN_LOCATE_AND_FAIL

/**
 * @brief Library context
 *
 * @param[in]	ctx	Library context to initialize
 * @return led_status_t LED_STATUS_SUCCESS on success, else error reason.
 */
led_status_t LED_SYM_PUBLIC led_new(struct led_ctx **ctx);

/**
 * @brief Free the library context
 *
 * @param[in]	ctx	Library context
 * @return led_status_t LED_STATUS_SUCCESS on success, else error reason.
 */
led_status_t LED_SYM_PUBLIC led_free(struct led_ctx *ctx);

/**
 * @brief Set the library file descriptor that the library should write log messages to
 *
 * @param[in]	ctx	Library context
 * @param[in]	log_fd	An open file descriptor that library can write log messages to
 */
void LED_SYM_PUBLIC led_log_fd_set(struct led_ctx *ctx, int log_fd);

/**
 * @brief Set the library log level
 *
 * @param[in]	ctx	Library context
 * @param[in]	level	Enumerated logging level
 *
 * Notes:
 *  - Library defaults to LED_LOG_LEVEL_ERROR
 *  - You must set a valid open file descriptor before any log messages will be written.
 */
void LED_SYM_PUBLIC led_log_level_set(struct led_ctx *ctx, enum led_log_level_enum level);

/**
 * @brief Instructs the library to scan system hardware for block devices with LED support.
 * This needs to be called before any other library functions can be utilized.  Can be called to
 * update any changes of state to the hardware.
 *
 * @param[in]	ctx	Library context
 * @return led_status_t LED_STATUS_SUCCESS on success, else error reason.
 */
led_status_t LED_SYM_PUBLIC led_scan(struct led_ctx *ctx);

/**
 * @brief Used to lookup a block device node to name used by library.  This function should be
 * called with its output being used as input for functions: led_is_management_supported, led_set.
 *
 * @param[in]	ctx			Library context
 * @param[in]	name			Device node to lookup
 * @param[out]	normalized_name		Normalized device name, size >= PATH_MAX
 * @return led_status_t LED_STATUS_SUCCESS on success, else error reason.
 *
 * Note: both parameters are expected to have a size of PATH_MAX
 */
led_status_t LED_SYM_PUBLIC led_device_name_lookup(struct led_ctx *ctx, const char *name,
						   char *normalized_name);

/**
 * @brief Given a block device path, returns if it has LED hardware support via library
 *
 * @param[in]	ctx	Library context
 * @param[in]	path	Device path, this is the result of led_device_name_lookup
 * @return enum led_cntrl_type which indicates which controller supports this device path,
 *		LED_CNTRL_TYPE_UNKNOWN if not supported
 */
enum led_cntrl_type LED_SYM_PUBLIC led_is_management_supported(struct led_ctx *ctx,
							       const char *path);

/**
 * @brief Set the ibpi pattern for the specified device
 *
 * @param[in]	ctx	Library context
 * @param[in]	path	Device path, this is the result of led_device_name_lookup
 * @param[in]	ibpi	Current ibpi value
 * @return led_status_t LED_STATUS_SUCCESS on success, else error reason.
 *
 * Note: Needs to be followed with led_flush()
 */
led_status_t LED_SYM_PUBLIC led_set(struct led_ctx *ctx, const char *path,
				    enum led_ibpi_pattern ibpi);

/**
 * @brief Flush the changes to hardware, this function is required after calling 1 or more calls
 * to led_set for the changes to take effect.  This function is not needed when using the slot API.
 *
 * @param[in]	ctx	Library context
 */
void LED_SYM_PUBLIC led_flush(struct led_ctx *ctx);

/**
 * @brief Checks to see if the specified controller type supports slots
 *
 * @param[in	cntrl	Enumerated controller to check for slot support
 * @return true	If controller supports slots API
 * @return false If controller does not support slots API
 */
bool LED_SYM_PUBLIC led_controller_slot_support(enum led_cntrl_type cntrl);


/**
 * @brief The following opaque data and functions are for dealing with information retrieval
 *        for slots.
 */

/**
 * @brief Represents a slot list entry
 *
 */
struct led_slot_list_entry;

/**
 * @brief Represents a slot list
 *
 */
struct led_slot_list;

/**
 * @brief Retrieve the led_slot_list opaque data type to use with led_slot_next and get functions
 *
 * @param[in]	ctx	Library context
 * @param[out]	sl	Opaque data type for slot list iterator
 * @return led_status_t LED_STATUS_SUCCESS on success, else error reason.
 */
led_status_t LED_SYM_PUBLIC led_slots_get(struct led_ctx *ctx, struct led_slot_list **sl);

/**
 * @brief Frees all memory associated with opaque data type led_slot_list
 *
 * @param[in]	sl	Valid slot list to free
 */
void LED_SYM_PUBLIC led_slot_list_free(struct led_slot_list *sl);

/**
 * @brief Resets the iterator, follow with led_slot_next or led_slot_prev
 *
 * @param[in]	sl	Valid slot list to reset iterator
 */
void LED_SYM_PUBLIC led_slot_list_reset(struct led_slot_list *sl);

/**
 * @brief Iterator to retrieve the next opaque data type led_slot_list_entry
 *
 * @param[in]	sl	 Opaque data type for slot list
 * @return struct led_slot_list_entry* or NULL if no more are available
 */
struct led_slot_list_entry LED_SYM_PUBLIC *led_slot_next(struct led_slot_list *sl);

/**
 * @brief Iterator to retrieve the previous opaque data type led_slot_list_entry
 *
 * @param[in]	sl	 Opaque data type for slot list
 * @return struct led_slot_list_entry* or NULL if no more are available
 */
struct led_slot_list_entry LED_SYM_PUBLIC *led_slot_prev(struct led_slot_list *sl);

/**
 * @brief Retrieve the slot device identifier
 *
 * @param[in]	se	Slot entry of interest
 * @return string pointer representing device id, NULL if empty
 *
 * Note:
 * - This pointer has a lifetime of ctx or until reset is called.
 *   Copy value if you need longer lifetime.
 * - Not all slots have device identifier
 */
const LED_SYM_PUBLIC char *led_slot_device(struct led_slot_list_entry *se);

/**
 * @brief Retrieve the slot identifier
 *
 * @param[in]	se	Slot entry of interest
 * @return string pointer representing slot id
 *
 * Note: This pointer has a lifetime of ctx or until reset is called.
 * Copy value if you need longer lifetime.
 */
const LED_SYM_PUBLIC char *led_slot_id(struct led_slot_list_entry *se);

/**
 * @brief Retrieve the persistent ID for the specified slot
 *
 * @param[in]	se	Slot entry of interest
 * @return string pointer representing persistent ID, NULL if not supported
 *
 * Note: This pointer has a lifetime of ctx or until reset is called.
 * Copy value if you need longer lifetime.
 */
const LED_SYM_PUBLIC char *led_slot_persistent_id(struct led_slot_list_entry *se);

/**
 * @brief Retrieve the enumerated slot type for the specified slot
 *
 * @param[in]	se	 Slot entry of interest
 * @return enum led_cntrl_type
 */
enum  led_cntrl_type LED_SYM_PUBLIC led_slot_cntrl(struct led_slot_list_entry *se);

/**
 * @brief Retrieve the ibpi value for the specified slot
 *
 * @param[in]	se	 Slot entry of interest
 * @return enum  led_ibpi_pattern
 */
enum  led_ibpi_pattern LED_SYM_PUBLIC led_slot_state(struct led_slot_list_entry *se);

/**
 * @brief The following functions are for get/set ibpi pattern for a specific slot
 */

/**
 * @brief Locate a slot entry by control type and slot identifier.
 *
 * @param[in]	ctx		The library context
 * @param[in]	cntrl		The controller type
 * @param[in]	slot_id		The slot identifier
 * @return NULL if entry not found, else slot entry, use led_slot_list_entry_free to free the
 *         returned value.
 */
LED_SYM_PUBLIC struct led_slot_list_entry *led_slot_find_by_slot(struct led_ctx *ctx,
								enum led_cntrl_type cntrl,
								char *slot_id);

/**
 * @brief Locate a slot entry by control type and device name.
 *
 * @param[in]	ctx		The library context
 * @param[in]	cntrl		The controller type
 * @param[in]	device_name	The slot device name (note: not all slots have a device name)
 * @return NULL if entry not found, else slot entry, use led_slot_list_entry_free to free the
 *         returned value.
 */
LED_SYM_PUBLIC struct led_slot_list_entry *led_slot_find_by_device_name(struct led_ctx *ctx,
							enum led_cntrl_type cntrl,
							char *device_name);

/**
 * @brief Frees a slot list entry, use with returned value of
 *        led_slot_find_by_slot and led_slot_find_by_device_name.
 *
 * @param[in]	se	Slot entry to free
 */
LED_SYM_PUBLIC void led_slot_list_entry_free(struct led_slot_list_entry *se);

/**
 * @brief Check if the specified slot supports persistent IDs
 *
 * @param[in]	ctx	Library context
 * @param[in]	cntrl	Controller type to check for persistent ID support
 * @return true if controller should support persistent IDs, false otherwise
 *
 * Note: This function is used to check if the controller should support persistent IDs. For any
 *       specific hardware implementation, this may or may not be true.
 */
bool LED_SYM_PUBLIC led_slot_persistent_id_support(struct led_ctx *ctx, enum led_cntrl_type cntrl);

/**
 * @brief Set the ipbi pattern for a specified slot
 *
 * @param[in]	ctx	Library context
 * @param[in]	se	Slot list entry, either retrieved from listing function of find function.
 * @param[in]	state	ibpi state
 * @return led_status_t LED_STATUS_SUCCESS on success, else error reason.
 */
led_status_t LED_SYM_PUBLIC led_slot_set(struct led_ctx *ctx, struct led_slot_list_entry *se,
					enum led_ibpi_pattern state);

#ifdef __cplusplus
}
#endif

#endif
