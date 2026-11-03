# Enclosure LED Utilities

Current version is 1.2.0.

Enclosure LED Utilities is a Linux project for monitoring and controlling storage device LEDs
on platforms. It enables visual identification and status indication (locate, fault, rebuild, etc.) for drives in enclosures and backplanes.

For more details and list of supported LED control protocols, refer to `ledmon(8)` man page.

The project provides following tools:
- **`ledmon`** — a daemon that monitors md RAID array and automatically updates drive LEDs to reflect array status.
- **`ledctl`** — a command-line tool for manually setting LED states on specific storage devices.
- **`libled`** — a shared library that exposes LED control functionality for integration into
  third-party applications.

-------------------------

## 1. Dependencies

-------------------------

The following packages are required for compilation:

|RHEL|SLES|Debian/Ubuntu|
|:---:|:---:|:---:|
| `pkgconf`, `RHEL7: pkgconfig`  | `pkg-config` | `pkg-config` |
| `automake` | `automake`   | `automake`   |
| `autoconf` | `autoconf`   | `autoconf`   |
| `autoconf-archive` | `autoconf-archive` | `autoconf-archive` |
| `gcc` | `gcc` | `gcc` |
 | `libtool` | `libtool` | `libtool` |
| `make` | `make` | `make` |
| `sg3_utils-devel`| `libsgutils-devel`  | `libsgutils2-dev` |
| `systemd-devel`  | `libudev-devel`     | `libudev-dev`     |
| `pciutils-devel` | `pciutils-devel`    | `libpci-dev`      |
 | `check-devel` | `check-devel` | `check` |

## 2. Configure package

-------------------------

Run `autogen.sh` to generate compilation configurations:
   `./autogen.sh`
   `./configure`

Run `./configure` with:
    `--enable-systemd` to configure with systemd service.

Run `./configure` with:
    `--enable-library` to enable building and installing the ledmon shared library,
    more library [information](src/lib/LIBRARY.md)

Run `./configure` with:
    `--enable-test` to enable building unit tests and adds a target for `make check`, requires `--enable-library`

Run `./configure` with:
    `--disable-doc` to disable building documentation.

## 3. Compiling the package

-------------------------

Run `make` command to compile the package.

## 4. Installing or uninstalling the package

-------------------------

Run the following command to install the package:
   `make install`

Run the following command to uninstall the package:
   `make uninstall`

## 5. Release notes

-------------------------

a. Enclosure LED Utilities are included as a part of RHEL, SLES and Debian/Ubuntu linux
   distributions.

b. For backplane enclosures attached to an iSCI controller support is limited to
   Intel(R) Intelligent Backplane.
