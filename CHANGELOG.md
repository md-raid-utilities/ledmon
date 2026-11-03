### v1.2.0

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v1.1.0...v1.2.0)

Enhancements

* Add support for the Linux kernel NPEM driver with backend selection and auto-detection
* Add persistent drive ID support to libled
* Report additional LED states for SES devices
* Add support for newer Dell systems
* Add AMD SGPIO slot management support to ledctl
* Add configurable failure LED persistence for reappearing non-RAID drives
* Add NVMe multipath device support to ledmon

Bug fixes

* Fix failure LED recovery and retry handling on VMD controllers
* Preserve failure LEDs for dropped RAID members
* Prevent incorrect slot-to-device matches across PCI path components
* Prevent crashes when comparing hot-removed devices without a controller
* Correct SES element index interpretation when EIIOE is set
* Fix IBPI pattern parsing and ledctl status reporting
* Fix memory leaks and a use-after-return issue
* Provide basename functionality on non-glibc systems

### v1.1.0

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v1.0.0...v1.1.0)

Enhancements

* Various enhancements in tests, licensing and deployment
* Add --default-controller command to ledctl

Bug fixes

* ledctl: add error message for missing devices
* ledctl: fix musl build failure by replacing on_exit() by atexit()
* Improve error handling for unsupported patters by falling back to normal
* Fix incorrect conversion of large integer values
* Fix compilation warnings
* Fix incorrect array index usage for block device and SES slot lookup

### v1.0.0 / 2024-02-28

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v0.97...v1.0.0)

Enhancements

* lib: introduce library
* tests: migrate tests to pytest
* Introduce "make check" option
* Allow disabling documentation installation
* Add minimal Nvme subsystem support
* Allow choosing output format from get-slot
* Rework handling --help option
* Update manual

Bug fixes

* Fix log severity, messages and level detection
* Ledctl: skip slot state check for set locate_off
* ci: Compile with tests and library enabled
* configure.ac: build library when "--enbable-test"
* Allow setting multiple LEDs per slot on SES
* Prevent compiler from optimizing out security checks
* Fix compilation warnings
* Add compiler defenses flags
* Add support for clang compiler
* utils.c: remove duplicated "-c" short parameter
* Remove parsing ALLOWLIST and EXCLUDELIST using regex
* ledctl: Fix exit ledctl with test flag

### v0.97 / 2023-05-16

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v0.96...v0.97)

Enhancements

* ledctl: add support to empty slots blinking
* ledmon license change to LGPLv2
* ledctl: Add SES get/set/list slot support
* Update NPEM wait command
* Remove exclusionary language
* ledmon: Define ONESHOT_NORMAL for VMD

Bug fixes

* ipmi: avoid error messages on non-dell platforms
* vmdssd: define normal pattern
* ledctl: clear unsupported params from config
* block.c: get_block_device_from_sysfs_path modification
* fix ibpi_value lists getter
* amd_ipmi: Allow to _enable_smbus_control
* ledmon.c: allocate memory for ignore
* sysfs: add only vmd devices to slots_list
* Rename --controller parameter
* Slots list implementations and fixes

### v0.96 / 2022-05-26

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v0.95...v0.96)

Bug fixes

* Manual updates, clarify --listed-only option
* Fix cache indexing of ATA port
* Fixes in regard to macros
* Fix memory leak in amd_ipmi.c
* Fix NULL pointer dereferences in sysfs.c
* Make messages appear in service log immediately
* Other minor fixes

### v0.95 / 2021-01-15

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v0.94...v0.95)

Enhancements

* Allow to run ledctl version without root
* README update with the compilation steps

Bug fixes

* Documentation updates
* Defaulting to SGPIO for AMD systems
* Don't rely on states priority while changing IBPI states
* Check the white/blacklist from ledmon.conf earlier in discovery
* Change installation directory to /usr/sbin
* Use package version from autotools, not version.h
* Fix memory leak in utils.c
* Bugfixes and refactoring in SES module
* Fixed issues reported by static analysis
* Build system fixes
* Other minor fixes

### v0.94 / 2020-02-04

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v0.93...v0.94)

Enhancements

* Support for AMD IPMI enclosure management
* Support for NPEM

Bug fixes

* Documentation updates
* Fix activity indicator state for SMP
* Fix for GCC 9 compilation
* Update ipbi pattern for drives with previous pattern unknown

### v0.93 / 2019-10-17

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v0.92...v0.93)

Enhancements

* Support for AMD SGPIO enclosure management
* Migration to GNU Autotools build system
* Added more strict compilation flags

Bug fixes

* Fixed segfault when a value is missing from ibpi_str
* Use proper format string with syslog()
* Fixed issues reported by static analysis
* Removed unused SGPIO structures
* Added udev_device reference clean-up
* Hidden ipmi error messages on non-dell platforms

### v0.92 / 2019-04-12

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v0.91-fixed...v0.92)

Bug fixes
* Silence warning and error messages.


### v0.91 / 2019-04-01

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v0.90...v0.91)

Enhancements

* Ledmon systemd service file.
* Shared configuration between ledmon and ledctl.
* Log-level support for ledctl.
* Build label support.
* 13G/14G Dell Servers support.
* Foreground option.

Bug fixes

* Udev action handling reimplementation.
* Unify ping process method.
* Recognize volumes under reshape.
* Distinguish inactive state for volume and container.
* Fix various gcc and clang warnings.
* Fix ledctl exit status.
* Logging method reimplementation.
* Makefile fixes.
* Change outdated functions and simplify string modifications.
* Ommited errors handling.


### v0.90 / 2018-02-14

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v0.80...v0.90)

Enhancements

* Handle udev events in ledmon.
* Possibility to list all controllers detected by LED utilities tool (ledctl --list-controllers).
* Configuration file for ledmon advanced features (check man ledmon.config).
* Added option to ledctl for managing only listed devices (ledctl --listed-only).
* Documentation improvements.

Bug fixes

* Detecting nvme disks during scan.
* Keep failure state after VMD reconnecting.
* Blinking failure LED after removing disk from RAID.
* Refactoring of SES-2 protocol implementation. SES minor fixes.
* Logfile and log levels small improvements.


### v0.80 / 2016-10-28

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v0.70...v0.80)

Enhancements

* Support for NVMe SSD devices.
* Support for NVMe devices under VMD domains.
* Using SMP GPIO_REG_TYPE_TX register for SGPIO.
* Sending LED commands optimization.
* Documentation improvements.

Bug fixes

* Fix support for the Dell PCIe SSD devices.
* Handling enclosure device name change.
* Fixes around IBPI_PATTERN_LOCATE_OFF state.


### v0.70 / 2012-12-12

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v0.40...v0.70)

Enhancements

* Introduce SES-2 protocol support.

Bug fixes

* Minor fixes.
* Memory leaks.


### v0.40 / 2012-07-12

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v0.3...v0.40)

Enhancements

* Support for Dell backplane bays.
* Turn off all unset LEDs in ledctl.

Bug fixes

* IPBI pattern interpretation.


### v0.3 / 2012-03-06

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/v0.2...v0.3)

Enhancements

* Support for disk drivers directly attached to SCU HBA.

Removals

* Remove dependency of smp_utils.


### v0.2 / 2011-08-24

[Commit list](https://github.com/md-raid-utilities/ledmon/compare/af8f20626e4e36cdf4bb9955fc65f22fec155580...v0.2)

Enhancements

* Ledmon initial version.
* Visualize the state of arrays.
* Introduce daemon app "ledmon" and LED manual control app "ledctl".

