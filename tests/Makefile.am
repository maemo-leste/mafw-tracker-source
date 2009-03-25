#
# Makefile.am for MAFW tracker source library.
#
# Author: Visa Smolander <visa.smolander@nokia.com>
#
# Copyright (C) 2007, 2008, 2009 Nokia. All rights reserved.

TESTS				= mafwtrackersourcetest

noinst_PROGRAMS			= $(TESTS)

AM_CFLAGS			= $(_CFLAGS)
AM_LDFLAGS			= $(_LDFLAGS)

DEPS				= $(top_builddir)/libmafw-tracker-source/mafw-tracker-source.la

# -----------------------------------------------
# Header files useful for testing programs
# -----------------------------------------------

INCLUDES			= -I$(top_srcdir) \
        			  -I$(top_srcdir)/libmafw-tracker-source \
				  $(DEPS_CFLAGS) \
				  $(SQLITE_DEPS_CFLAGS) \
        			  $(CHECKMORE_CFLAGS)

# -----------------------------------------------
# Libraries needed to build the tests programs
# -----------------------------------------------

LDADD				= $(CHECKMORE_LIBS)					\
        			  $(DEPS_LIBS) \
				  $(SQLITE_DEPS_LIBS) \
        			  $(top_builddir)/libmafw-tracker-source/mafw-tracker-source.la


# -----------------------------------------------
# Test programs build specs
# -----------------------------------------------

mafwtrackersourcetest_SOURCES	= check-main.c \
        			  check-mafwtrackersource.c

# -----------------------------------------------
# Clean up everything on maintainer-clean
# -----------------------------------------------

MAINTAINERCLEANFILES		= Makefile.in

# Run valgrind on tests.
VG_OPTS                         := --leak-check=full --show-reachable=yes --suppressions=test.suppressions
vg: $(TESTS)
	for test in $^; do \
	G_SLICE='always-malloc' \
	G_DEBUG='gc-friendly' \
	libtool --mode=execute valgrind $(VG_OPTS) $$test 2>vglog.$$test; \
	done;
	-rm -f vgcore.*

