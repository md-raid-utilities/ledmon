# SPDX-License-Identifier: GPL-3.0-or-later

AC_PREREQ([2.69])


m4_define([MAJOR_VERSION], 1)
m4_define([MINOR_VERSION], 1)
m4_define([MICRO_VERSION], 0)

AC_INIT([ledmon],[MAJOR_VERSION.MINOR_VERSION.MICRO_VERSION])
AC_CONFIG_MACRO_DIR([m4])
AC_SUBST([PACKAGE_DATE], "2024")
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

# If CFLAGS was not set, do not set default compiler flags, leave it empty.
# https://www.gnu.org/savannah-checkouts/gnu/autoconf/manual/autoconf-2.72/autoconf.html#index-AC_005fPROG_005fCC-1
: ${CFLAGS=""}

LT_PREREQ([2.4.2])
AM_PROG_AR
LT_INIT

# Checks for programs.
AC_PROG_CC[gcc clang]
AX_CHECK_PROG([make])


# Using the version parts we will construct the appropriate libtool version.
# Following semantic versioning (X.Y.Z) we will bump X when we have an incompatible
# library version, bump Y when we add a new public function to the library and, bump
# Z when we fix a bug/improve the library internals. We can only add to library, we
# can never remove for backwards compatibility
# eg.
# start 0.97.0
# Add 1 or more library functions, eg. led_foo and do a release -> 0.97.0
# Fix 1 or more bugs in project and do a release -> 0.97.1
# Fix 1 or more bugs and add API additions and do a release -> 0.98.0
#
# Full details for semantic versioning:
# https://semver.org/
#

CURRENT=`expr MAJOR_VERSION '*' 1 + MINOR_VERSION`
AGE=MINOR_VERSION
REVISION=MICRO_VERSION
LIBLED_LIBTOOL_VERSION=$CURRENT:$REVISION:$AGE

AC_SUBST([LIBLED_LIBTOOL_VERSION])

AC_PROG_CC
AC_PROG_INSTALL

AC_CONFIG_HEADERS([config_ac.h])

# Add compiler flags.
AM_CFLAGS='-Wall -I../config -O2'
AM_CPPFLAGS='-D_DEBUG -D_GNU_SOURCE -D_DEFAULT_SOURCE -DDMALLOC_DISABLE -DBUILD_LABEL=\""$(BUILD_LABEL)"\"'

AC_DEFUN([AX_AM_CFLAGS_ADD],[AX_CHECK_LINK_FLAG($1, AM_CFLAGS="$AM_CFLAGS $1")])
AX_AM_CFLAGS_ADD([-Wformat -Wformat-security])
AX_AM_CFLAGS_ADD([-Wformat-overflow=2])
AX_AM_CFLAGS_ADD([-Wno-strict-overflow])
AX_AM_CFLAGS_ADD([-fno-delete-null-pointer-checks])
AX_AM_CFLAGS_ADD([-Wwrapv])
AX_AM_CFLAGS_ADD([-Wformat-truncation=1])
AX_AM_CFLAGS_ADD([-Wshift-negative-value])
AX_AM_CFLAGS_ADD([-Walloca])
AX_AM_CFLAGS_ADD([-Wmissing-field-initializers])
AX_AM_CFLAGS_ADD([-Wformat-signedness])
AX_AM_CFLAGS_ADD([-D_FORTIFY_SOURCE=2])
AX_AM_CFLAGS_ADD([-fstack-protector-strong])
AX_AM_CFLAGS_ADD([-fPIE])

# Add linker flags.
AC_DEFUN([AX_AM_LDFLAGS_ADD],[AX_CHECK_LINK_FLAG($1, AM_LDFLAGS="$AM_LDFLAGS $1")])
AX_AM_LDFLAGS_ADD([-pie])
AX_AM_LDFLAGS_ADD([[-Wl,-z,relro]])
AX_AM_LDFLAGS_ADD([[-Wl,-z,now]])
AX_AM_LDFLAGS_ADD([[-Wl,-z,noexecstack]])

# Add library compiler flags.
AM_LIB_CFLAGS=''
AC_DEFUN([AX_AM_LIB_CFLAGS_ADD],[AX_CHECK_COMPILE_FLAG($1, AM_LIB_CFLAGS="$AM_LIB_CFLAGS $1")])
AX_AM_LIB_CFLAGS_ADD([-fPIC -shared])
AX_AM_LIB_CFLAGS_ADD([-fvisibility=hidden])

AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_CPPFLAGS])
AC_SUBST([AM_LDFLAGS])
AC_SUBST([AM_LIB_CFLAGS])
AC_PREFIX_DEFAULT(/usr)

# Automake 1.11 - silent build rules
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

PKG_PROG_PKG_CONFIG

# Checks for libraries.
AC_CHECK_LIB([rt], [shm_unlink], [], [AC_MSG_ERROR(librt not found)])
AC_CHECK_LIB([sgutils2], [sg_ll_send_diag], [], [AC_MSG_ERROR(libsgutils not found)])
PKG_CHECK_MODULES([LIBUDEV], [libudev])
PKG_CHECK_MODULES([LIBPCI], [libpci])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h inttypes.h limits.h stdint.h stdlib.h string.h sys/file.h sys/ioctl.h sys/param.h sys/time.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_CHECK_MEMBERS([struct stat.st_rdev])
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_CHECK_FUNCS([ftruncate memset munmap realpath regcomp select strcasecmp strchr strdup strerror strrchr strstr strtol strtoul])

# configure options
AC_ARG_ENABLE(systemd, AS_HELP_STRING([--enable-systemd], [install ledmon systemd service]))

AS_IF([test "x$enable_systemd" = xyes], [SYSTEMD_STR=yes], [SYSTEMD_STR=no])

AM_CONDITIONAL([SYSTEMD_CONDITION], [test "$SYSTEMD_STR" = yes])

# target directory for ledmon service file
SYSTEMD_PATH="$(pkg-config systemd --variable=systemdsystemunitdir)"
AC_SUBST([SYSTEMD_PATH])

# Add configure option to build without library
AC_ARG_ENABLE([library],
    [AS_HELP_STRING([--enable-library],
        [enable building ledmon library])],
    [with_library=${enableval}],
    [with_library=no])

AC_ARG_ENABLE([test],
    [AS_HELP_STRING([--enable-test],
                    [enable test cases, run using  'make check', requires --enable-library])],
    [with_test=${enableval}],
    [with_test=no])

AM_CONDITIONAL([WITH_TEST], [test "x$with_test" = "xyes"])

if test "x${with_test}" = "xyes"; then
    PKG_CHECK_MODULES([LIBCHECK], [check >= 0.9.8 ])
    # We need the library enabled if we are going to run the tests
    if test "x${with_library}" = "xno"; then
        AC_MSG_ERROR([--enable-library is required when specifying --enable-test])
    fi
    AC_SUBST([ENABLE_TEST], '-DENABLE_TEST')
fi

AM_CONDITIONAL([WITH_LIBRARY], [test "x$with_library" = "xyes"])

# Add configure option to disable documentation building
AC_ARG_ENABLE([doc],
    [AS_HELP_STRING([--disable-doc],
        [do not install ledmon documentation])],
    [with_doc=${enableval}],
    [with_doc=yes])

AM_CONDITIONAL([WITH_DOC], [test "x$with_doc" = "xyes"])

AC_CONFIG_FILES([Makefile
                 ledmon.pc
                 doc/Makefile
                 src/Makefile
                 src/common/Makefile
                 src/lib/Makefile
                 src/lib/include/Makefile
                 src/lib/include/led/Makefile
                 src/ledctl/Makefile
                 src/ledmon/Makefile
                 systemd/Makefile
                 tests/Makefile])
AC_OUTPUT
AC_MSG_RESULT([
$PACKAGE_NAME $VERSION configuration:

  Source code location:    ${srcdir}
  Preprocessor flags:      ${AM_CPPFLAGS} ${CPPFLAGS} ${ENABLE_TEST}
  C compiler flags:        ${AM_CFLAGS} ${CFLAGS}
  Linker flags:            ${AM_LDFLAGS} ${LDFLAGS}
  Common install location: ${prefix}
  configure parameters:    --enable-systemd=${SYSTEMD_STR} --enable-library=${with_library} --enable-test=${with_test} --enable-doc=${with_doc}
])
