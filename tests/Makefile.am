# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2024 Intel Corporation.

AM_CPPFLAGS = -I@srcdir@./src/lib/include

EXTRA_DIST=lib_unit_test.c block_match_test.c tail_match_test.c

if WITH_TEST
all: lib_unit_test block_match_test tail_match_test

check_PROGRAMS = lib_unit_test block_match_test tail_match_test
lib_unit_test_CFLAGS = $(AM_CFLAGS) $(LIBCHECK_CFLAGS)
lib_unit_test_LDADD = ../src/lib/libled.la $(LIBCHECK_LIBS)
lib_unit_test_SOURCES = lib_unit_test.c

# block_match_test exercises an internal (hidden-visibility) helper, so it links
# the convenience archive rather than the public shared library.
block_match_test_CFLAGS = $(AM_CFLAGS) $(LIBCHECK_CFLAGS) -I$(top_srcdir)/src/lib -I$(top_srcdir)/src
block_match_test_LDADD = ../src/lib/libledinternal.la $(LIBCHECK_LIBS)
block_match_test_SOURCES = block_match_test.c

# tail_match_test exercises an internal (hidden-visibility) helper, so it links
# the convenience archive rather than the public shared library.
tail_match_test_CFLAGS = $(AM_CFLAGS) $(LIBCHECK_CFLAGS) -I$(top_srcdir)/src/lib -I$(top_srcdir)/src
tail_match_test_LDADD = ../src/lib/libledinternal.la $(LIBCHECK_LIBS)
tail_match_test_SOURCES = tail_match_test.c
endif
